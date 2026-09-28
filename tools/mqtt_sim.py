#!/usr/bin/env python3
"""가짜 노드 시뮬레이터 — 실제 장치 없이 브로커·디스플레이·HA를 시험한다.

UPower 인버터, JBD BMS, RTU 릴레이 보드(8채널) 세 노드를 흉내 내어 실제 노드와 같은 토픽·페이로드를
발행한다 (docs/15-roles.md §3.3, §4). 값은 시간에 따라 바뀌고, <prefix>/switch/<name>/set 명령을
받으면 상태를 바꿔 다시 발행하므로 디스플레이 터치로 스위치를 켜고 끌 수 있다.

    pip install paho-mqtt
    python tools/mqtt_sim.py --host 192.168.1.136            # Ctrl+C로 종료 (offline 발행)
    python tools/mqtt_sim.py --host broker.local --clear     # 남은 retained 토픽 지우기

주의: 실제 노드와 같은 prefix(rv/upower, rv/bms, rv/rtu)를 쓰므로 실제 노드가 붙어 있는 브로커에서는
--root 로 다른 루트를 쓰고 디스플레이의 display.topic_root도 맞춘다.
"""
import argparse
import json
import math
import random
import signal
import sys
import time

import paho.mqtt.client as mqtt

UPOWER_META = {
    "type": "upower", "label": "Inverter", "ring": "flows:pv.chg_w,grid.in_w,inv.out_w",
    "switches": [{"n": "inverter", "l": "Inverter"}, {"n": "gridout_prio", "l": "Grid Output Priority"},
                 {"n": "solar_charge", "l": "Solar Charge"}, {"n": "grid_charge", "l": "Grid Charge"}],
    "metrics": [{"l": "Battery SOC", "u": "%", "p": "bat.soc"}, {"l": "PV Charge Power", "u": "W", "p": "pv.chg_w"},
                {"l": "Grid In Power", "u": "W", "p": "grid.in_w"}, {"l": "Inverter Out Power", "u": "W", "p": "inv.out_w"},
                {"l": "Battery Voltage", "u": "V", "p": "bat.v"}],
}
BMS_META = {
    "type": "jbdbms", "label": "BMS", "ring": "battery:soc,power",
    "switches": [{"n": "charge_fet", "l": "Charge MOSFET"}, {"n": "discharge_fet", "l": "Discharge MOSFET"}],
    "metrics": [{"l": "SOC", "u": "%", "p": "soc"}, {"l": "Power", "u": "W", "p": "power"},
                {"l": "Pack Voltage", "u": "V", "p": "pack_v"}, {"l": "Current", "u": "A", "p": "current"},
                {"l": "Cell Voltage Diff", "u": "V", "p": "cell_diff"}],
}
# 디스플레이 홈 바로가기(Inverter·Mover·Pump·Lights·Drain·Fill)와 이름을 맞춘 채널
RTU_NAMES = ["Pump", "Lights", "Mover", "Drain", "Fill", "Fridge", "Heater", "Aux"]
RTU_META = {
    "type": "rtusw_mk1", "label": "Switch",
    "switches": [{"n": f"ch{i + 1}", "l": n} for i, n in enumerate(RTU_NAMES)],
    "metrics": [],
}


class Sim:
    def __init__(self, client, root, period):
        self.c = client
        self.root = root
        self.period = period
        self.t0 = time.time()
        self.soc = 72.0
        self.sw = {
            "upower": {"inverter": True, "gridout_prio": False, "solar_charge": True, "grid_charge": False},
            "bms": {"charge_fet": True, "discharge_fet": True},
            "rtu": {f"ch{i + 1}": i in (1, 2) for i in range(8)},
        }
        self.meta = {"upower": UPOWER_META, "bms": BMS_META, "rtu": RTU_META}

    def pub(self, topic, payload, retain=True):
        if not isinstance(payload, str):
            payload = json.dumps(payload, separators=(",", ":"))
        self.c.publish(f"{self.root}/{topic}", payload, qos=0, retain=retain)

    def announce(self):
        for node, meta in self.meta.items():
            self.pub(f"{node}/meta", meta)
            self.pub(f"{node}/availability", "online")
            self.publish_switches(node)

    def publish_switches(self, node):
        for name, on in self.sw[node].items():
            self.pub(f"{node}/switch/{name}/state", "ON" if on else "OFF")

    def on_message(self, _c, _u, msg):
        # <root>/<node>/switch/<name>/set
        parts = msg.topic.split("/")
        if len(parts) != 5 or parts[2] != "switch" or parts[4] != "set":
            return
        node, name = parts[1], parts[3]
        if node not in self.sw or name not in self.sw[node]:
            return
        on = msg.payload.decode(errors="ignore").strip().upper() == "ON"
        self.sw[node][name] = on
        print(f"  cmd {node}/{name} -> {'ON' if on else 'OFF'}")
        self.pub(f"{node}/switch/{name}/state", "ON" if on else "OFF")

    def step(self):
        t = time.time() - self.t0
        up = self.sw["upower"]
        # 태양광: 2분 주기로 0~2.4kW 사이를 오르내림 (Solar Charge가 꺼지면 0)
        pv_w = max(0.0, 2400 * math.sin(t / 120 * math.pi) + random.uniform(-40, 40)) if up["solar_charge"] else 0.0
        load_w = (380 + 120 * math.sin(t / 17) + random.uniform(-20, 20)) if up["inverter"] else 0.0
        grid_w = (900.0 + random.uniform(-30, 30)) if up["grid_charge"] else 0.0
        bat_w = pv_w + grid_w - load_w
        if not self.sw["bms"]["charge_fet"]:
            bat_w = min(bat_w, 0.0)
        if not self.sw["bms"]["discharge_fet"]:
            bat_w = max(bat_w, 0.0)
        # 10kWh 팩 기준, 시연용으로 시간을 60배 빠르게
        self.soc = min(100.0, max(5.0, self.soc + bat_w * self.period / 3600 / 10000 * 100 * 60))
        pack_v = 48.0 + self.soc / 100 * 6.4 + bat_w / 5000
        current = bat_w / pack_v

        self.pub("upower/state", {
            "pv": {"in_v": round(96 + random.uniform(-2, 2), 1) if pv_w else 0, "in_a": round(pv_w / 96, 2),
                   "in_w": round(pv_w), "chg_v": round(pack_v, 2), "chg_a": round(pv_w / pack_v, 2),
                   "chg_w": round(pv_w), "kwh": round(12.3 + t / 3600, 3), "temp": 31, "state": 1 if pv_w else 0},
            "grid": {"in_v": 230.1, "in_a": round(grid_w / 230, 2), "in_w": round(grid_w), "chg_v": round(pack_v, 2),
                     "chg_a": round(grid_w / pack_v, 2), "chg_w": round(grid_w), "kwh": 4.2, "temp": 29},
            "inv": {"in_v": round(pack_v, 2), "out_v": 230.0 if up["inverter"] else 0, "out_a": round(load_w / 230, 2),
                    "out_w": round(load_w), "hz": 60.0 if up["inverter"] else 0},
            "bypass": {"v": 0, "a": 0, "w": 0},
            "bat": {"v": round(pack_v, 2), "temp": 24, "soc": round(self.soc), "state": 1 if bat_w > 0 else 2},
        })

        cells = [round(pack_v / 16 + random.uniform(-0.006, 0.006), 3) for _ in range(16)]
        self.pub("bms/state", {
            "pack_v": round(pack_v, 2), "current": round(current, 2), "power": round(bat_w, 1),
            "remain_ah": round(200 * self.soc / 100, 1), "full_ah": 200.0, "soc": round(self.soc),
            "cycles": 87, "cell_diff": round(max(cells) - min(cells), 3),
            "chg_fet": self.sw["bms"]["charge_fet"], "dis_fet": self.sw["bms"]["discharge_fet"],
            "protection": 0, "balance": 0, "cell_count": 16, "cell_v": cells, "ntc": [24.1, 25.3],
        })

        self.pub("rtu/state", {"ch": [self.sw["rtu"][f"ch{i + 1}"] for i in range(8)]})
        print(f"t={t:5.0f}s  pv={pv_w:6.0f}W  load={load_w:5.0f}W  bat={bat_w:+6.0f}W  soc={self.soc:5.1f}%")

    def offline(self):
        for node in self.meta:
            self.pub(f"{node}/availability", "offline")

    def clear(self):
        # 빈 retained 페이로드 = 삭제 (브로커 보관분도 지운다)
        for node, meta in self.meta.items():
            for sub in ("meta", "state", "availability"):
                self.c.publish(f"{self.root}/{node}/{sub}", b"", retain=True)
            for s in meta["switches"]:
                self.c.publish(f"{self.root}/{node}/switch/{s['n']}/state", b"", retain=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="broker.local")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user")
    ap.add_argument("--password")
    ap.add_argument("--root", default="rv", help="토픽 루트 (display.topic_root와 같게)")
    ap.add_argument("--period", type=float, default=2.0, help="state 발행 간격(초)")
    ap.add_argument("--duration", type=float, default=0, help="이 시간(초) 뒤 종료, 0 = 무한")
    ap.add_argument("--clear", action="store_true", help="시뮬레이터 retained 토픽을 지우고 종료")
    args = ap.parse_args()

    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"essio-sim-{random.randint(0, 0xFFFF):04X}")
    if args.user:
        c.username_pw_set(args.user, args.password)
    sim = Sim(c, args.root, args.period)
    c.on_message = sim.on_message
    c.on_connect = lambda cl, *_: (cl.subscribe(f"{args.root}/+/switch/+/set"), sim.announce())
    c.connect(args.host, args.port, keepalive=30)
    c.loop_start()

    if args.clear:
        time.sleep(0.5)
        sim.clear()
        time.sleep(0.5)
        c.loop_stop()
        print("cleared")
        return

    stop = False

    def on_signal(*_):
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)
    print(f"publishing to {args.host}:{args.port} under '{args.root}/' — Ctrl+C to stop")
    end = time.time() + args.duration if args.duration else None
    time.sleep(0.5)
    while not stop and (end is None or time.time() < end):
        sim.step()
        time.sleep(args.period)
    sim.offline()
    time.sleep(0.5)
    c.loop_stop()
    c.disconnect()
    print("stopped (availability=offline)")


if __name__ == "__main__":
    sys.exit(main())
