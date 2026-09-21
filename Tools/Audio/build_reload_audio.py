"""生成原创短机械换弹音效；48kHz单声道16bit，无外部录音/许可依赖，事件由动画清单定义。"""
import json
import math
import random
import struct
import wave
from pathlib import Path

# 工程相对输出，仅覆盖本制作器拥有的换弹WAV和规格清单。
ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "Art/Audio/Reload"
RATE = 48000  # 采样率Hz；运行时交由UE重采样，不使用压缩音频制作源。
PROFILES = {"Pistol": (1.35, .13), "Rifle": (1.0, .18), "Shotgun": (.72, .23), "Sniper": (.86, .22)}  # 音色倍率/秒长度体现不同机构重量。
EVENTS = ("MagOut", "MagIn", "MagSeat", "SlideRelease", "ChargingHandle", "BoltOpen", "BoltClose")  # 稳定事件ID，与动作manifest绑定。


def synthesize(model, event):
    """model/event决定可复现的原创机械瞬态；返回16位样本和声学检查数据，不含长混响。"""
    print(f"[CALL] ReloadAudio.synthesize {model}/{event}", flush=True)
    # pitch/duration为型号音色；操作声长度缩短使当前最快换弹也能清楚分辨事件。
    pitch, duration = PROFILES[model]
    duration *= .7 if event in ("MagIn", "MagSeat", "BoltClose") else 1.0
    # 独立固定种子使重建字节稳定，各武器/动作仍有不同噪声纹理。
    rng = random.Random(f"BreachReload-v1:{model}:{event}")
    samples = []  # 浮点工作缓冲，最后统一峰值保护后转PCM。
    low = 0.0  # 噪声低通状态，仅属于本次音效合成。
    previous = 0.0  # 前一个噪声值，用于较细的金属高频层。
    for index in range(round(duration * RATE)):
        # t为本声音内秒；多个撞击偏移形成弹匣卡榫/机械回弹而非纯音蜂鸣。
        t = index / RATE
        noise = rng.uniform(-1.0, 1.0)
        low = .87 * low + .13 * noise
        value = 0.0
        for offset, gain in ((0.0, 1.0), (.022, .38), (.057, .20)):
            # elapsed只在该瞬态后参与合成，负时间不产生预响。
            elapsed = t - offset
            if elapsed < 0:
                continue
            decay = math.exp(-elapsed * (58 if event in ("MagSeat", "BoltClose") else 34))
            # 多个非谐波共振叠加，短冲击带宽远大于单频振荡。
            metal = (math.sin(2 * math.pi * 1180 * pitch * elapsed) + .43 * math.sin(2 * math.pi * 2473 * pitch * elapsed)) * math.exp(-elapsed * 92)
            body = math.sin(2 * math.pi * 174 * pitch * elapsed) * math.exp(-elapsed * 48)
            value += gain * (.42 * noise * decay + .13 * metal + .33 * body)
        if event in ("MagOut", "ChargingHandle", "BoltOpen"):
            # 滑动层使用平滑包络，随金属冲击收尾；不会生成持续循环底噪。
            value += .26 * (noise - previous) * math.sin(math.pi * min(t / duration, 1)) ** 2 * math.exp(-t * 12)
        value += .08 * low * math.exp(-t * 24)
        # 两端各1.5ms淡入/淡出，防止音频组件中断或PCM边界的直流点击。
        value *= min(1.0, t / .0015, max(0.0, (duration - t) / .0015))
        samples.append(value)
        previous = noise
    peak = max(abs(value) for value in samples)  # 峰值归一到-6dBFS，留出多事件重叠余量。
    samples = [round(value / peak * .50 * 32767) for value in samples]
    rms = math.sqrt(sum((value / 32768) ** 2 for value in samples) / len(samples))  # 用于发现静音或削波错误。
    return samples, rms


def main():
    """离线生成WAV及清单；音效事件对齐由UE导入器读取机械动画manifest负责。"""
    print("[CALL] ReloadAudio.main", flush=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    manifest = []  # 每个实际波形的规格记录，方便独立验证和重建。
    for model in PROFILES:
        for event in EVENTS:
            # 只交付模型实际使用的机构声，避免无意义的通用重复资源。
            if event == "SlideRelease" and model != "Pistol":
                continue
            if event in ("ChargingHandle", "BoltOpen", "BoltClose") and model == "Pistol":
                continue
            if event == "ChargingHandle" and model == "Sniper":
                continue
            samples, rms = synthesize(model, event)
            name = f"SW_Reload_{model}_{event}"  # 与UE SoundWave命名一致。
            with wave.open(str(OUTPUT / (name + ".wav")), "wb") as output:
                output.setnchannels(1)
                output.setsampwidth(2)
                output.setframerate(RATE)
                output.writeframes(struct.pack("<" + "h" * len(samples), *samples))
            manifest.append({"name": name, "model": model, "event": event, "sample_rate": RATE,
                             "channels": 1, "seconds": len(samples) / RATE, "rms": rms, "peak_dbfs": -6.02})
    (OUTPUT / "reload_audio_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"RELOAD_AUDIO_BUILD_SUCCESS sounds={len(manifest)}", flush=True)


if __name__ == "__main__":
    main()
