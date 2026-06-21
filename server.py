# -*- coding: utf-8 -*-
# Сервер управления трёхпалым самоцентрирующимся захватом.
# Версия для проверки на компьютере (без железа).
#
# Состояние хранится на сервере И ЗАПИСЫВАЕТСЯ В ФАЙЛ:
#   state.json   - текущее состояние (читается при запуске, чтобы не терялось)
#   history.log  - журнал всех команд с временем (история)
#
# Запуск:
#   pip install flask
#   python server.py
# Затем открой в браузере: http://127.0.0.1:5000

from flask import Flask, request, jsonify, send_from_directory
import os
import json
from datetime import datetime

app = Flask(__name__)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
STATE_FILE = os.path.join(BASE_DIR, "state.json")
HISTORY_FILE = os.path.join(BASE_DIR, "history.log")

# ------- Параметры захвата (физика устройства) -------
MIN_MM = 10      # минимальный зазор между губками (полностью сжато)
MAX_MM = 80      # максимальный зазор (полностью раскрыто)

# ------- Состояние по умолчанию -------
state = {
    "percent": 100,        # 0 = сжато, 100 = раскрыто
    "gap_mm": MAX_MM,      # текущий зазор в мм
    "moving": False,       # двигатель сейчас крутится?
    "status": "open"       # open / closed / partial / moving
}


def load_state():
    """Прочитать состояние из файла при запуске сервера (если файл есть)."""
    global state
    if os.path.exists(STATE_FILE):
        try:
            with open(STATE_FILE, "r", encoding="utf-8") as f:
                state = json.load(f)
        except (ValueError, OSError):
            pass  # если файл повреждён - оставляем состояние по умолчанию


def save_state():
    """Записать текущее состояние в файл state.json."""
    with open(STATE_FILE, "w", encoding="utf-8") as f:
        json.dump(state, f, ensure_ascii=False, indent=2)


def log_command(cmd):
    """Дописать строку в журнал history.log: время, команда, состояние."""
    line = "{}  {:<8}  percent={} gap={}mm status={}\n".format(
        datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        cmd, state["percent"], state["gap_mm"], state["status"]
    )
    with open(HISTORY_FILE, "a", encoding="utf-8") as f:
        f.write(line)


def percent_to_mm(p):
    """Перевод процента раскрытия в зазор в мм."""
    p = max(0, min(100, p))
    return round(MIN_MM + (MAX_MM - MIN_MM) * p / 100.0, 1)


def apply_percent(p, cmd):
    """Установить новое положение, пересчитать состояние и записать в файлы."""
    p = int(max(0, min(100, p)))
    state["percent"] = p
    state["gap_mm"] = percent_to_mm(p)
    state["moving"] = False
    if p <= 0:
        state["status"] = "closed"
    elif p >= 100:
        state["status"] = "open"
    else:
        state["status"] = "partial"
    # В версии для ESP32 здесь будет команда шаговому двигателю на нужное число шагов.
    save_state()       # записываем текущее состояние в файл
    log_command(cmd)   # дописываем строку в журнал


@app.route("/")
def index():
    return send_from_directory(BASE_DIR, "index.html")


@app.route("/grip")
def grip():
    """Сжать губки к центру до упора."""
    apply_percent(0, "grip")
    return jsonify(state)


@app.route("/release")
def release():
    """Раскрыть губки полностью."""
    apply_percent(100, "release")
    return jsonify(state)


@app.route("/stop")
def stop():
    """Остановить двигатель (положение остаётся текущим)."""
    state["moving"] = False
    save_state()
    log_command("stop")
    return jsonify(state)


@app.route("/set")
def set_position():
    """Установить раскрытие в процентах: /set?percent=50"""
    try:
        p = int(request.args.get("percent", state["percent"]))
    except ValueError:
        return jsonify({"error": "percent must be a number"}), 400
    apply_percent(p, "set")
    return jsonify(state)


@app.route("/status")
def status():
    """Текущее состояние захвата. Панель опрашивает раз в секунду."""
    return jsonify(state)


if __name__ == "__main__":
    load_state()  # при старте читаем сохранённое состояние из файла
    app.run(host="0.0.0.0", port=5000)
