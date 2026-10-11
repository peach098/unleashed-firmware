#!/usr/bin/env python3
"""
pm3_matrix.py — этап 0 плана pm3-parity: прогон матрицы команд ISO15693/SLIX2
через Proxmark3 и сбор эталона «запрос → байты и задержка ответа».

Один и тот же прогон делается для реальной карты и для Flipper, эмулирующего
её дамп; затем `report --compare` кладёт результаты рядом.

Примеры:
  # карта ZONCOLAN, 3 повтора, порт по умолчанию /dev/ttyACM0
  ./pm3_matrix.py run --label card_AC --uid AC839A01090104E0
  # та же карта на Flipper (эмуляция дампа с тем же UID)
  ./pm3_matrix.py run --label flipper_AC --uid AC839A01090104E0
  # с EAS-блоком (меняет состояние EAS и возвращает его назад в конце)
  ./pm3_matrix.py run --label card_AC --uid AC839A01090104E0 --eas
  # отчёт и сравнение
  ./pm3_matrix.py report out/2026-10-11 --compare card_AC flipper_AC

UID задаётся В ЭФИРНОМ ПОРЯДКЕ, как в `hf 15 raw` (LSB first, заканчивается на E0).

Каждый образец = отдельный запуск клиента pm3:
  pm3 -p PORT --incognito -f -c "<команды>; trace list -t 15; trace save -f <файл>"
Результаты: <out>/<label>_<name>_<i>.log и .trace, сводка <out>/summary_<label>.json.

Формат .trace (little-endian): u32 ts | u16 dur | u16 (len:15 | isResp:1) | frame | parity[ceil(len/8)].
В режиме ридера PM3 пишет ts в тактах несущей 13.56 МГц (armsrc/iso15693.c: LogTrace_ISO15693(..., t*4, ...),
SSP clk = fc/4). Задержка ответа = (ts_resp - (ts_req + dur_req)) / 13.56 мкс.
Сверить единицы с колонками Start/End в `trace list -t 15` при первом прогоне.
"""
import argparse, glob, json, os, re, struct, subprocess, sys, time, datetime

FC_MHZ = 13.56
# В .trace поле ts хранится в тактах несущей 13.56 МГц, а поле dur — в единицах
# по 32 такта (так `trace list -t 15` считает End = Start + dur*32). Сверено с
# колонками Start/End на первом образце rd_00 (req 384+1712*32=55168;
# resp 61504+1024*32=94272) — см. README, раздел «Единицы задержки».
DUR_TICKS = 32
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# ----------------------------------------------------------------------------
# матрица команд. {U} = UID в эфирном порядке; {S} = слот карты (младший ниббл
# первого эфирного байта UID), {NS} = несовпадающий слот.
# флаги: 0x22 addressed, 0x02 non-addressed, 0x12 selected, 0x26/0x06 inventory 1/16 slot.
# ----------------------------------------------------------------------------
def raw(hexcmd, extra=""):
    return f"hf 15 raw -a -c {extra}-d {hexcmd}".replace("  ", " ")

MATRIX = [
    # inventory
    ("inv_1slot",            [raw("260100")],                       "эталон 1-slot"),
    ("inv_16slot",           [raw("060100")],                       "16 слотов, маска 0: карта отвечает в слоте {S}"),
    ("inv_16slot_mask_ok",   [raw("060104{S:02X}")],                "маска 4 бита = слот карты"),
    ("inv_16slot_mask_bad",  [raw("060104{NS:02X}")],               "маска 4 бита не совпадает: ожидаем тишину"),
    # read single block
    ("rd_00",                [raw("2220{U}00")],                    ""),
    ("rd_4F",                [raw("2220{U}4F")],                    "последний блок (счётчик)"),
    ("rd_50",                [raw("2220{U}50")],                    "ВНЕ ПАМЯТИ: расхождение №3/4"),
    ("rd_FF",                [raw("2220{U}FF")],                    "вне памяти"),
    ("rd_50_nonaddr",        [raw("022050")],                       "вне памяти, non-addressed"),
    ("rd_50_selected",       [raw("2225{U}", "-k "), "hf 15 raw -c -d 122050"], "вне памяти, selected"),
    # read multiple blocks
    ("rdm_00_07",            [raw("2223{U}0007")],                  "8 блоков"),
    ("rdm_00_1F",            [raw("2223{U}001F")],                  "32 блока"),
    ("rdm_00_3F",            [raw("2223{U}003F")],                  "64 блока"),
    ("rdm_00_4F",            [raw("2223{U}004F")],                  "80 блоков = 323 байта: расхождение №1"),
    ("rdm_40_0F",            [raw("2223{U}400F")],                  "последние 16 блоков"),
    ("rdm_40_10",            [raw("2223{U}4010")],                  "за край на 1 блок"),
    ("rdm_00_FF",            [raw("2223{U}00FF")],                  "больше памяти"),
    # system / security
    ("sysinfo",              [raw("222B{U}")],                      ""),
    ("blocksec_00_4F",       [raw("222C{U}004F")],                  ""),
    ("blocksec_00_50",       [raw("222C{U}0050")],                  "за край"),
    # NXP custom
    ("nxp_sysinfo",          [raw("22AB04{U}")],                    ""),
    ("nxp_getrandom",        [raw("22B204{U}")],                    "случайный по определению"),
    ("nxp_eas_alarm",        [raw("22A504{U}")],                    "EAS ALARM: активность и 32-байтная последовательность"),
    ("nxp_unknown_A8",       [raw("22A804{U}")],                    "неизвестная custom-команда"),
    ("nxp_unknown_A8_nonaddr", [raw("02A804")],                     "неизвестная custom-команда, non-addressed"),
]

# EAS-блок меняет состояние метки; включается флагом --eas. В конце состояние
# возвращается к исходному (по строке "EAS ... is active" первого `hf 15 info`).
EAS_STEPS = [
    ("eas_info_0",        ["hf 15 info"]),
    ("eas_enable_pwd",    ["hf 15 slixeasenable -p 00000000"]),
    ("eas_info_1",        ["hf 15 info"]),
    ("eas_disable_pwd",   ["hf 15 slixeasdisable -p 00000000"]),
    ("eas_info_2",        ["hf 15 info"]),
    ("eas_enable_nopwd",  ["hf 15 slixeasenable"]),
    ("eas_info_3",        ["hf 15 info"]),
    ("eas_disable_nopwd", ["hf 15 slixeasdisable"]),
    ("eas_info_4",        ["hf 15 info"]),
]

RESULT_RE = re.compile(r"^\[(?:\+|!!?|=|#|\?)\]\s*(.*)$")
BANNER = ("Session log", "Using UART", "Communicating", "Output will be flushed",
          "No preferences file", "Waiting for Proxmark3", "Reconnecting", "Trace")
# PM3 печатает имя файла в обратных кавычках: Saved N bytes to binary file `/path.trace`
SAVED_RE = re.compile(r"[Ss]aved\s+\d+\s+bytes to .*?file\s+[`'\"]?(/?\S+?\.trace)[`'\"]?\s*$", re.M)


def detect_port():
    """Порт Proxmark3: /dev/serial/by-id/*roxmark* или единственный /dev/ttyACM*."""
    import glob
    cands = [os.path.realpath(x) for x in glob.glob("/dev/serial/by-id/*") if "roxmark" in x.lower()]
    if cands:
        return cands[0]
    acm = sorted(glob.glob("/dev/ttyACM*"))
    for dev in acm:
        try:
            man = open(f"/sys/class/tty/{os.path.basename(dev)}/../../../manufacturer").read()
            if "proxmark" in man.lower():
                return dev
        except OSError:
            pass
    return acm[0] if len(acm) == 1 else None


def parse_hw_version(text):
    """(client_version, os_version) из вывода `hw version`; os_version=None, если устройство не ответило.
    Поддерживает старый формат ('client: ...' / 'os: ...') и новый ('[ Client ]' блок, 'OS......... ...')."""
    client = os_ = None
    section = None
    for ln in text.splitlines():
        st = ln.strip()
        if st.startswith("[ Client ]"):
            section = "client"; continue
        if st.startswith("[ ARM ]"):
            section = "arm"; continue
        if st.startswith("[ ") and st.endswith(" ]"):
            section = None; continue
        m = re.match(r"^(?:client|os)\s*[:.]+\s*(.+)$", st, re.I)
        if m and st.lower().startswith("client"):
            client = m.group(1).strip(); continue
        if m and st.lower().startswith("os"):
            os_ = m.group(1).strip(); continue
        if section == "client" and client is None and st.startswith("Iceman/"):
            client = st
        if section == "arm" and st.startswith("OS"):
            os_ = re.sub(r"^OS[. ]*", "", st).strip()
    return client, os_


def run_pm3(pm3, port, cmds, log_path, timeout=120):
    joined = "; ".join(cmds)
    argv = [pm3] + (["-p", port] if port else []) + ["--incognito", "-f", "-c", joined]
    t0 = time.time()
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, errors="replace", timeout=timeout)
        out, rc = p.stdout, p.returncode
    except subprocess.TimeoutExpired as e:
        out, rc = (e.stdout or "") + "\n[matrix] pm3 client timeout\n", -1
    with open(log_path, "w") as f:
        f.write(f"$ {' '.join(argv)}\n")
        f.write(out)
    return ANSI.sub("", out), rc, time.time() - t0


def extract_results(clean_out):
    """Строки статуса команд до начала вывода trace list."""
    lines = []
    for ln in clean_out.splitlines():
        if ln.strip().startswith("Start |") or "trace list" in ln and lines:
            break
        m = RESULT_RE.match(ln.strip())
        if m and not m.group(1).startswith(BANNER) and not m.group(1).startswith("Saved "):
            lines.append(m.group(1).strip())
        elif "OFFLINE" in ln or "cannot communicate" in ln:
            lines.append(ln.strip())
    return lines


def cmd_run(a):
    uid = a.uid.upper().replace(" ", "")
    if not re.fullmatch(r"[0-9A-F]{16}", uid):
        sys.exit("UID: 16 hex-символов в эфирном порядке, например AC839A01090104E0")
    if not uid.endswith("E0"):
        print("ВНИМАНИЕ: UID не заканчивается на E0; для ISO15693 эфирный порядок заканчивается на E0", file=sys.stderr)
    slot = int(uid[0:2], 16) & 0xF
    nslot = slot ^ 0xF
    out = os.path.abspath(a.out or os.path.join(os.path.dirname(__file__), "out",
                                                datetime.date.today().isoformat()))
    os.makedirs(out, exist_ok=True)
    only = set(a.only.split(",")) if a.only else None

    steps = [(n, c, note) for (n, c, note) in MATRIX if not only or n in only]
    if a.write_block is not None:
        steps.append(("wr_%02X" % a.write_block, None, "чтение блока и запись того же содержимого"))
    if a.eas:
        steps += [(n, c, "EAS") for (n, c) in EAS_STEPS]

    if not a.port:
        a.port = detect_port()
        if a.port:
            print(f"порт PM3 определён автоматически: {a.port}")
        else:
            print("порт не найден автоматически: полагаюсь на обёртку pm3 (-p не передаю)")

    # предварительная проверка связи с устройством
    pre, _, _ = run_pm3(a.pm3, a.port, ["hw version"], os.path.join(out, f"{a.label}_preflight_hw_version.log"))
    client_ver, os_ver = parse_hw_version(pre)
    if "OFFLINE" in pre or "cannot communicate" in pre or os_ver is None:
        print("ОШИБКА: клиент PM3 не соединился с устройством (см. preflight-лог).", file=sys.stderr)
        print("Проверьте порт (ls -l /dev/serial/by-id/), права (группа uucp) и что порт не занят;",
              "передайте --port /dev/ttyACMx явно.", file=sys.stderr)
        sys.exit(2)
    print(f"client: {client_ver}\nos:     {os_ver}")
    if client_ver and client_ver.split()[0] != os_ver.split()[0]:
        print("ВНИМАНИЕ: версии клиента и прошивки различаются; замеры 16-slot невалидны.", file=sys.stderr)

    summary = {"label": a.label, "uid_onair": uid, "slot": slot, "port": a.port,
               "started": datetime.datetime.now().isoformat(timespec="seconds"), "samples": []}
    eas_initial_active = None
    print(f"[{a.label}] UID {uid}, slot {slot}, out {out}")

    for name, cmds, note in steps:
        repeats = 1 if name.startswith("eas_") else a.repeat
        for i in range(1, repeats + 1):
            base = os.path.join(out, f"{a.label}_{name}_{i}")
            if cmds is None:  # write-block: read, then write back the same data
                rd, _, _ = run_pm3(a.pm3, a.port, [raw(f"2220{uid}{a.write_block:02X}")], base + "_pre.log")
                m = re.search(r"\(7\)\s+00((?:\s+[0-9A-F]{2}){4})", rd)
                if not m:
                    print(f"  {name}#{i}: не удалось прочитать блок, запись пропущена")
                    continue
                data = m.group(1).replace(" ", "")
                real_cmds = [raw(f"2221{uid}{a.write_block:02X}{data}", "-w ")]
            else:
                real_cmds = [c.format(U=uid, S=slot, NS=nslot) for c in cmds]
            # `trace clear` очищает буфер трассы устройства до команды, иначе каждый
            # следующий образец дописывается в общий буфер и .trace растёт лавиной.
            full = ["trace clear"] + real_cmds + ["trace list -t 15", f"trace save -f {base}"]
            clean, rc, dt = run_pm3(a.pm3, a.port, full, base + ".log")
            res = extract_results(clean)
            saved = SAVED_RE.search(clean)
            trace_path = saved.group(1).strip("`'\" ") if saved else (base + ".trace" if os.path.exists(base + ".trace") else None)
            if trace_path is None:
                res.append("[matrix] trace file not saved")
            sample = {"name": name, "i": i, "note": note, "cmds": real_cmds, "rc": rc,
                      "results": res, "trace": trace_path,
                      "log": base + ".log", "wall_s": round(dt, 2)}
            if name == "eas_info_0":
                eas_initial_active = "is active" in clean and "not active" not in clean
                sample["eas_initial_active"] = eas_initial_active
            summary["samples"].append(sample)
            print(f"  {name}#{i}: {' | '.join(res[-3:]) if res else '(нет строк статуса)'}")
            with open(os.path.join(out, f"summary_{a.label}.json"), "w") as f:
                json.dump(summary, f, ensure_ascii=False, indent=1)
            time.sleep(a.pause)

    if a.eas and eas_initial_active:
        print("  восстанавливаю EAS = active")
        run_pm3(a.pm3, a.port, ["hf 15 slixeasenable -p 00000000"], os.path.join(out, f"{a.label}_eas_restore.log"))
    print(f"готово: {out}/summary_{a.label}.json")


# ----------------------------------------------------------------------------
# report
# ----------------------------------------------------------------------------
def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc


def parse_trace(path):
    """Разбор .trace. Путь может прийти в обратных кавычках (так его печатает PM3);
    если файла нет — пробуем убрать/добавить суффикс -001 (PM3 так избегает перезаписи)."""
    path = resolve_trace(path)
    if not path:
        return []
    try:
        buf = open(path, "rb").read()
    except OSError:
        return []
    out, off, n = [], 0, len(buf)
    while off + 8 <= n:
        ts, dur, lr = struct.unpack_from("<IHH", buf, off)
        dl, isr = lr & 0x7FFF, bool(lr >> 15)
        s = off + 8
        if dl == 0 or dl > 4096 or s + dl > n:
            break
        fr = buf[s:s + dl]
        off = s + dl + ((dl - 1) // 8 + 1)
        out.append({"ts": ts, "dur": dur, "resp": isr, "frame": fr, "crc_ok": len(fr) >= 3 and crc16(fr) == 0xF0B8})
    return out


REPORT_DIR = None  # каталог отчёта; по нему ищем .trace по basename (пути в summary абсолютные)


def resolve_trace(path):
    """Нормализовать путь к .trace: снять кавычки, найти вариант с/без -001,
    при отсутствии — по basename в каталоге отчёта (summary хранит абсолютные пути)."""
    if not path:
        return None
    p = path.strip().strip("`'\" ")
    cands = []
    for base in (p, os.path.join(REPORT_DIR, os.path.basename(p)) if REPORT_DIR else None):
        if not base:
            continue
        stem = base[:-6] if base.endswith(".trace") else base
        cands += [base, stem + ".trace", stem + "-001.trace", re.sub(r"-001$", "", stem) + ".trace"]
    for cand in cands:
        if os.path.exists(cand):
            return cand
    return None


def hx(b):
    return " ".join(f"{x:02X}" for x in b)


def cmd_payload(sample):
    """Эфирные байты последней команды образца (payload из `-d ...`, без CRC)."""
    for c in reversed(sample.get("cmds") or []):
        m = re.search(r"-d\s+([0-9A-Fa-f]+)", c)
        if m:
            try:
                return bytes.fromhex(m.group(1))
            except ValueError:
                return None
    return None


# Байтовый ответ, декодированный клиентом: `(N) xx xx ..` в любом месте строки
# (в т.ч. после `Slot  9: ` у 16-slot inventory).
STATUS_OK_RE = re.compile(r"\((\d+)\)\s+((?:[0-9A-Fa-f]{2}\s+)*[0-9A-Fa-f]{2})\b")
STATUS_FAIL_RE = re.compile(r"command failed|No answer|timeout|No valid responses|OFFLINE|cannot communicate", re.I)


def status_text(sample):
    """Ответ так, как его декодировал клиент PM3: либо байты `(N) ..`, либо причина отказа."""
    for ln in reversed(sample.get("results") or []):
        m = STATUS_OK_RE.search(ln)
        if m:
            return m.group(2).strip().upper()
    for ln in reversed(sample.get("results") or []):
        if STATUS_FAIL_RE.search(ln):
            return ln.strip()
    return None


def _has_resp(st):
    """st — результат status_text: байты => True, причина отказа/None => False."""
    return bool(st) and not STATUS_FAIL_RE.search(st)


def analyse(sample):
    """Найти в трассе кадр ИМЕННО этой команды и парный ему ответ (следующий кадр).

    Буфер трассы PM3 между образцами мог не очищаться, поэтому брать «последний
    запрос / первый ответ по ts» нельзя: ts сбрасывается в каждой транзакции, и
    матчится чужой кадр (напр. ответ INVENTORY). Матчим кадр запроса по байтам
    команды и берём непосредственно следующий кадр, если он ответ."""
    ev = parse_trace(sample.get("trace"))
    payload = cmd_payload(sample)
    info = {"frames": len(ev), "req": None, "resp": None, "crc_ok": None,
            "delay_us": None, "status": status_text(sample)}
    if not ev or payload is None:
        return info
    matches = [i for i, e in enumerate(ev)
               if not e["resp"] and e["frame"][:len(payload)] == payload]
    # Источник истины о том, ответила ли карта на ЭТУ команду в ЭТОМ повторе —
    # декодированная клиентом строка статуса; трасса (буфер мог не чиститься между
    # образцами) даёт только задержку/CRC и лишь когда статус = байтовый ответ.
    has_resp = _has_resp(info["status"])
    chosen = next((i for i in matches if i + 1 < len(ev) and ev[i + 1]["resp"]), None)
    if chosen is None and matches:
        chosen = matches[-1]
    if chosen is not None:
        info["req"] = hx(ev[chosen]["frame"])
    if has_resp and chosen is not None and chosen + 1 < len(ev) and ev[chosen + 1]["resp"]:
        req, r = ev[chosen], ev[chosen + 1]
        info["resp"] = hx(r["frame"])
        info["crc_ok"] = r["crc_ok"]
        info["delay_us"] = round((r["ts"] - (req["ts"] + req["dur"] * DUR_TICKS)) / FC_MHZ, 1)
    return info


def _norm(s):
    return (s or "").replace(" ", "").upper()


def _answer(info):
    """Каноничный ответ команды = декодированный клиентом статус (байты / тишина).
    Трасса-резерв только если строки статуса нет вовсе."""
    st = info["status"]
    if st is None:
        return info["resp"]
    if STATUS_FAIL_RE.search(st):
        return None  # тишина / отказ
    return st


def _pair_table(md, name_order, rows, labels):
    """Таблица образцов для пары/набора labels + список расхождений (возвращает diffs)."""
    md.append("| команда | label | ответ (байты) | CRC | задержка, мкс (3 повтора) | повторы согласны | заметка |")
    md.append("|---|---|---|---|---|---|---|")
    diffs = []
    # команды матрицы в порядке объявления, затем прочие (wr_, eas_) по алфавиту
    matrix_names = [n for n, _, _ in MATRIX]
    ordered = matrix_names + sorted(k for k in rows if k not in matrix_names)
    note_by_name = {n: note for n, _, note in MATRIX}
    for name in ordered:
        if name not in rows:
            continue
        ans_by_label = {}
        for lb in labels:
            items = rows[name].get(lb)
            if not items:
                continue
            infos = [i for _, i in items]
            answers = [_answer(i) for i in infos]
            consistent = len(set(answers)) == 1
            delays = [i["delay_us"] for i in infos if i["delay_us"] is not None]
            dtxt = "/".join(f"{d:g}" for d in delays) if delays else "—"
            ans = _answer(infos[0])
            crc = infos[0]["crc_ok"]
            crc_s = "" if crc is None else ("ok" if crc else "BAD")
            disp = "тишина/отказ" if ans is None else (ans if len(ans) <= 60 else ans[:57] + "…")
            md.append(f"| {name} | {lb} | {disp} | {crc_s} | {dtxt} | "
                      f"{'да' if consistent else '**НЕТ**'} | {note_by_name.get(name, '')} |")
            ans_by_label[lb] = ans
        if len(labels) == 2 and len(ans_by_label) == 2 and name not in ("nxp_getrandom", "nxp_eas_alarm"):
            a0, a1 = (ans_by_label[labels[0]], ans_by_label[labels[1]])
            if _norm(a0) != _norm(a1):
                diffs.append((name, a0, a1))
    md.append("")
    if len(labels) == 2:
        md.append(f"### Расхождения {labels[0]} vs {labels[1]}: **{len(diffs)}**")
        md.append(f"*(исключены nxp_getrandom и nxp_eas_alarm — ответ по определению меняется)*")
        md.append("")
        for name, a0, a1 in diffs:
            md.append(f"- **{name}**: `{labels[0]}` = {a0 or '*тишина/отказ*'}; "
                      f"`{labels[1]}` = {a1 or '*тишина/отказ*'}")
        md.append("")
    return diffs


def _auto_pairs(sums):
    """Пары card_X / flipper_X по общему суффиксу."""
    pairs = []
    for lb in sums:
        if lb.startswith("card_"):
            suf = lb[len("card_"):]
            if f"flipper_{suf}" in sums:
                pairs.append((lb, f"flipper_{suf}"))
    return pairs


def cmd_report(a):
    global REPORT_DIR
    REPORT_DIR = a.dir
    sums = {}
    for fn in sorted(os.listdir(a.dir)):
        if fn.startswith("summary_") and fn.endswith(".json"):
            s = json.load(open(os.path.join(a.dir, fn)))
            sums[s["label"]] = s
    if not sums:
        sys.exit("нет summary_*.json в " + a.dir)

    if a.compare:
        pairs = [tuple(a.compare)]
    else:
        pairs = _auto_pairs(sums) or [tuple(list(sums)[:2])]

    md = [f"# PM3 parity report — {os.path.basename(os.path.normpath(a.dir))}", "",
          f"Сгенерировано: {datetime.datetime.now().isoformat(timespec='seconds')}  ",
          f"Источник: `{a.dir}`  ",
          f"Задержка ответа = (ts_resp − (ts_req + dur_req·{DUR_TICKS})) / {FC_MHZ} МГц "
          f"(сверено с колонками Start/End `trace list -t 15`).", ""]
    all_diffs = {}
    for labels in pairs:
        if any(lb not in sums for lb in labels):
            continue
        md.append(f"## {labels[0]} ↔ {labels[1]}")
        md.append("")
        rows = {}
        for lb in labels:
            for smp in sums[lb]["samples"]:
                rows.setdefault(smp["name"], {}).setdefault(lb, []).append((smp, analyse(smp)))
        all_diffs[labels] = _pair_table(md, [n for n, _, _ in MATRIX], rows, list(labels))

    text = "\n".join(md)
    with open(os.path.join(a.dir, "report.md"), "w") as f:
        f.write(text + "\n")
    for labels, d in all_diffs.items():
        print(f"{labels[0]} vs {labels[1]}: расхождений {len(d)}")
    print(f"report.md записан: {os.path.join(a.dir, 'report.md')}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    r = sp.add_parser("run")
    r.add_argument("--label", required=True, help="card_AC, card_79, flipper_AC, ...")
    r.add_argument("--uid", required=True, help="UID в эфирном порядке, 16 hex")
    r.add_argument("--port", help="порт PM3; по умолчанию автоопределение (/dev/serial/by-id, /dev/ttyACM*)")
    r.add_argument("--pm3", default="pm3")
    r.add_argument("--repeat", type=int, default=3)
    r.add_argument("--pause", type=float, default=0.5, help="пауза между запусками, с")
    r.add_argument("--out", help="каталог результатов (по умолчанию out/<дата>)")
    r.add_argument("--only", help="подмножество имён через запятую")
    r.add_argument("--eas", action="store_true", help="выполнить EAS-блок (меняет и восстанавливает состояние EAS)")
    r.add_argument("--write-block", type=lambda s: int(s, 0), help="прочитать блок и записать то же содержимое")
    r.set_defaults(fn=cmd_run)
    p = sp.add_parser("report")
    p.add_argument("dir")
    p.add_argument("--compare", nargs=2, metavar=("LABEL_A", "LABEL_B"))
    p.set_defaults(fn=cmd_report)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
