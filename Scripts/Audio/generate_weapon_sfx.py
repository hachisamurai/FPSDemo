"""离线生成四枪各三个开火变体及试听；仅依赖 NumPy/标准库，无录音或在线 API。

函数形参的单位/生命周期见各 docstring；中间数组仅在本次同步制作期间有效。
狙击拉栓按用户要求不制作。旧 Sword/Hit 源资源完全不由本脚本管理。
"""
from pathlib import Path
import hashlib
import json
import math
import wave
import numpy as np

# 稳定源目录；可以从任意工作目录执行，不直接写 UE 二进制资产。
ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "SourceAssets/Audio/SFX/Weapons/Breach"
# 核心音效统一为 48kHz、mono、PCM24；源峰值统一 -3dBFS，Cue 另留混音余量。
RATE = 48000
PEAK = 10 ** (-3 / 20)
# 字段依次为文件秒数、随机音高范围、确定性种子；分层配方在独立函数中定义。
PROFILES = {
    "Pistol": (0.28, (0.97, 1.03), 17371),
    "Rifle": (0.36, (0.98, 1.02), 39541),
    "Shotgun": (0.40, (0.98, 1.02), 59663),
    "Sniper": (1.20, (0.98, 1.02), 79589),
}


def envelope(time, decay, delay=0.0, attack=0.00035):
    """time 为秒数组；decay/attack/delay 为秒，返回延迟后快速起音、指数衰减包络。"""
    print(f"[FourWeaponSFX] envelope decay={decay} delay={delay}")
    # age 钳制只供指数计算；门控确保 delay 之前保持绝对静音。
    age = np.maximum(time - delay, 0.0)
    return (1 - np.exp(-age / attack)) * np.exp(-age / decay) * (time >= delay)


def band_noise(rng, count, low, high):
    """rng 为本变体局部随机源；count 为样本数，low/high 为 Hz；返回 RMS=1 的平滑带限噪声。"""
    print(f"[FourWeaponSFX] band_noise band={low}..{high}")
    # 离线频域整形先于幅度包络，不过滤已起音的枪声，从而避免起音前的预振铃。
    frequency = np.fft.rfftfreq(count, 1 / RATE)
    # 软截止使用 4 阶幅度斜率，避免砖墙滤波产生长时间振铃；DC 显式清零。
    safe_frequency = np.maximum(frequency, 1.0)
    mask = 1 / np.sqrt(1 + (low / safe_frequency) ** 8)
    mask /= np.sqrt(1 + (safe_frequency / high) ** 8)
    mask[0] = 0
    # shaped 只包含本变体的独立激励；绝不共享四枪的基础采样波形。
    shaped = np.fft.irfft(np.fft.rfft(rng.standard_normal(count)) * mask, n=count)
    return shaped / max(float(np.sqrt(np.mean(shaped ** 2))), 1e-12)


def impact(time, frequency, decay, delay=0.0):
    """time/delay/decay 为秒，frequency 为 Hz；短非谐波冲击提供枪体重量，非长扫频音效。"""
    print(f"[FourWeaponSFX] impact frequency={frequency} delay={delay}")
    # age 用于模态相位；第二模态打破单纯低音鼓的谐波感，包络负责门控。
    age = np.maximum(time - delay, 0)
    return (np.sin(2 * np.pi * frequency * age)
            + 0.31 * np.sin(2 * np.pi * frequency * 1.731 * age)) * envelope(time, decay, delay)


def mechanical(rng, time, delay, weight=1.0):
    """rng/time 为当前音效上下文；delay 为秒，weight 为相对振幅；仅生成短机匣撞击而非拉栓/泵动。"""
    print(f"[FourWeaponSFX] mechanical delay={delay}")
    # click/shell 是同步局部层，轻微不规则模态在 20ms 内迅速衰减。
    click = band_noise(rng, len(time), 1200, 7000) * envelope(time, 0.0025, delay)
    shell = impact(time, float(rng.uniform(1250, 1750)), 0.006, delay + 0.001)
    return weight * (click + 0.4 * shell)


def pistol(rng, time):
    """rng/time 为本变体上下文；短高频拍击、小型中低频枪体、较晚轻滑套，返回未归一化波形。"""
    print("[FourWeaponSFX] pistol")
    # 各层独立噪声：低频止于约 200Hz，手枪不会变成缩小音量的重枪。
    transient = band_noise(rng, len(time), 1700, 5900) * envelope(time, 0.0028)
    body = band_noise(rng, len(time), 450, 2200) * envelope(time, 0.012)
    punch = impact(time, float(rng.uniform(210, 275)), 0.013)
    slide = mechanical(rng, time, float(rng.uniform(0.049, 0.067)), 0.09)
    tail = band_noise(rng, len(time), 650, 3400) * envelope(time, 0.022, 0.014, 0.003)
    return 1.3 * transient + 0.63 * body + 0.38 * punch + slide + 0.10 * tail


def rifle(rng, time):
    """rng/time 为本变体上下文；硬裂响、带饱和的中频机匣和短低频适配高速连射。"""
    print("[FourWeaponSFX] rifle")
    # 步枪中频粗糙度主导；低频时间常数 16ms，避免 750RPM 下持续叠加。
    crack = band_noise(rng, len(time), 2400, 7000) * envelope(time, 0.004)
    receiver = np.tanh(0.72 * band_noise(rng, len(time), 350, 2200)) * envelope(time, 0.021)
    punch = impact(time, float(rng.uniform(125, 150)), 0.016)
    action = mechanical(rng, time, float(rng.uniform(0.034, 0.046)), 0.18)
    reflection = band_noise(rng, len(time), 500, 2700) * envelope(time, 0.026, 0.061, 0.002)
    return 1.15 * crack + 1.5 * receiver + 0.51 * punch + action + 0.085 * reflection


def shotgun(rng, time):
    """rng/time 为本变体上下文；微错位散射爆发、宽中低频和快速自动机动作，不合成泵动声。"""
    print("[FourWeaponSFX] shotgun")
    # blast 由四个独立微爆发组成；delay/weight 为秒及相对振幅，不表现为四次射击。
    blast = np.zeros(len(time))
    for delay, weight in ((0, 1), (0.003, 0.58), (0.007, 0.42), (0.011, 0.26)):
        blast += weight * band_noise(rng, len(time), 850, 4400) * envelope(time, 0.0042, delay)
    # 中频厚度即使小音箱缺少超低频仍保留；饱和仅作用于主体层。
    body = np.tanh(0.65 * band_noise(rng, len(time), 150, 820)) * envelope(time, 0.023, 0.003)
    punch = impact(time, float(rng.uniform(85, 108)), 0.019, 0.001)
    action = mechanical(rng, time, float(rng.uniform(0.061, 0.079)), 0.16)
    tail = band_noise(rng, len(time), 350, 1800) * envelope(time, 0.027, 0.073, 0.003)
    return 0.76 * blast + 1.45 * body + 1.15 * punch + action + 0.105 * tail


def sniper(rng, time):
    """rng/time 为本变体上下文；0ms 锐裂声与延迟 18ms 重枪体形成双段结构，无拉栓层。"""
    print("[FourWeaponSFX] sniper")
    # boom 比 crack 晚到达；低频噪声叠加非谐冲击，避免做成带音高的电子炮。
    crack = band_noise(rng, len(time), 2200, 8200) * envelope(time, 0.006)
    boom = (impact(time, float(rng.uniform(68, 83)), 0.047, 0.018)
            + 0.45 * band_noise(rng, len(time), 55, 165) * envelope(time, 0.038, 0.018, 0.002))
    body = band_noise(rng, len(time), 190, 1400) * envelope(time, 0.043, 0.014, 0.002)
    air = band_noise(rng, len(time), 360, 2400) * envelope(time, 0.14, 0.048, 0.012)
    # echoes 是远场弱反射；每个反射独立激励，避免整段重复形成可辨认的第二声枪响。
    echoes = np.zeros(len(time))
    for delay, level in ((0.135, 0.075), (0.263, 0.046), (0.407, 0.026)):
        echoes += level * band_noise(rng, len(time), 220, 1500) * envelope(time, 0.098, delay, 0.011)
    return 1.55 * crack + 0.97 * boom + 0.56 * body + 0.105 * air + echoes


def finish(samples, peak):
    """samples 为未处理浮点 mono，peak 为目标样本峰值 (<1)；去 DC、亚声滤除、首尾平滑后归一化。"""
    print(f"[FourWeaponSFX] finish peak={peak}")
    # 35Hz 一阶因果高通只清除 DC/亚声，不使用整段 FFT 高通以免在枪声前引入预回响。
    result = np.empty_like(samples)
    alpha = math.exp(-2 * math.pi * 35 / RATE)
    previous_input = 0.0  # 上一输入样本，状态仅属于当前文件。
    previous_output = 0.0  # 上一高通输出，初始化零以保证起点无跳变。
    for index, value in enumerate(samples):  # index 为帧，value 为当前浮点振幅。
        previous_output = alpha * (previous_output + value - previous_input)
        result[index] = previous_output
        previous_input = value
    # 不把总波形压成砖墙；只保留层内轻饱和，整体仍有明显的爆点、主体和尾音。
    result[:24] *= np.linspace(0, 1, 24)
    result[-2400:] *= np.linspace(1, 0, 2400) ** 2
    result *= peak / max(float(np.max(np.abs(result))), 1e-12)
    if not np.isfinite(result).all():
        raise ValueError("Non-finite synthesized samples")
    return result


def write_wav(path, samples):
    """path 为本脚本输出目录路径；samples 为有限且 |x|<1 的浮点 mono，写实际 PCM24。"""
    print(f"[FourWeaponSFX] write_wav {path.name}")
    path.parent.mkdir(parents=True, exist_ok=True)
    if not np.isfinite(samples).all() or np.max(np.abs(samples)) >= 1:
        raise ValueError(f"Invalid/clipping source: {path}")
    # 定点 PCM24 打包；整数采用有符号 32bit 承载，输出低三字节而非伪标头。
    encoded = np.rint(samples * ((1 << 23) - 1)).astype(np.int32)
    payload = np.stack((encoded & 255, (encoded >> 8) & 255, (encoded >> 16) & 255), axis=1).astype(np.uint8)
    with wave.open(str(path), "wb") as writer:  # 文件句柄仅在 with 内有效。
        writer.setparams((1, 3, RATE, len(samples), "NONE", "not compressed"))
        writer.writeframes(payload.tobytes())


def inspect(path, duration):
    """path 为磁盘 WAV，duration 为预期秒数；实际解码检查格式、4x 插值峰值、能量和边界。"""
    print(f"[FourWeaponSFX] inspect {path.name}")
    with wave.open(str(path), "rb") as reader:  # 不信任生成内存数组，重新读取头和全部 PCM。
        spec = reader.getparams()
        payload = np.frombuffer(reader.readframes(spec.nframes), dtype=np.uint8).reshape(-1, 3).astype(np.int32)
    assert (spec.nchannels, spec.sampwidth, spec.framerate, spec.nframes) == (1, 3, RATE, round(duration * RATE))
    # 解码 24bit 二补码，samples 为磁盘实际量化波形。
    integers = payload[:, 0] | (payload[:, 1] << 8) | (payload[:, 2] << 16)
    integers = (integers ^ (1 << 23)) - (1 << 23)
    samples = integers / float(1 << 23)
    # 4x/32tap Lanczos 插值估算重建峰值，不冒充认证 true-peak 仪表。
    reconstructed = float(np.max(np.abs(samples)))
    for fraction in (0.25, 0.5, 0.75):  # fraction 是原采样间的分数相位。
        distance = np.arange(-15, 17) - fraction
        kernel = np.sinc(distance) * np.sinc(distance / 16)
        kernel /= kernel.sum()
        reconstructed = max(reconstructed, float(np.max(np.abs(np.convolve(samples, kernel, mode="same")))))
    # 有意义起音阈值为 -60dBFS；尾 5ms 的 RMS 必须低于 -60dBFS。
    onset = int(np.flatnonzero(np.abs(samples) > 0.001)[0]) / RATE
    tail_rms = float(np.sqrt(np.mean(samples[-240:] ** 2)))
    assert np.isfinite(samples).all() and reconstructed < 1.0
    assert onset < 0.003 and integers[0] == 0 and integers[-1] == 0 and tail_rms < 0.001
    assert abs(20 * math.log10(float(np.max(np.abs(samples)))) + 3.0) < 0.01
    assert abs(float(np.mean(samples))) < 0.0001
    # 低頻尾占比定位连续射击堆积：<200Hz 离线频域隔离后，统计 80ms 以后的能量占比。
    frequency = np.fft.rfftfreq(len(samples), 1 / RATE)
    low = np.fft.irfft(np.fft.rfft(samples) / np.sqrt(1 + (frequency / 200) ** 8), n=len(samples))
    energy = samples ** 2
    return {"file": path.relative_to(ROOT).as_posix(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "sample_rate_hz": RATE, "bit_depth": 24, "channels": 1, "duration_seconds": duration,
            "sample_peak_dbfs": round(20 * math.log10(float(np.max(np.abs(samples)))), 4),
            "estimated_4x_peak_dbfs": round(20 * math.log10(reconstructed), 4),
            "rms_dbfs": round(10 * math.log10(float(np.mean(energy))), 3),
            "onset_ms": round(onset * 1000, 3), "tail_5ms_rms_dbfs": round(20 * math.log10(max(tail_rms, 1e-12)), 3),
            "energy_after_180ms_ratio": round(float(energy[8640:].sum() / energy.sum()), 6),
            "low_energy_after_80ms_ratio": round(float((low[3840:] ** 2).sum() / (low ** 2).sum()), 6),
            "clipped_samples": 0, "first_sample": int(integers[0]), "last_sample": int(integers[-1])}


def make_preview(name, voices, rate, count):
    """name 为试听名；voices 是本枪变体列表，rate 为每秒枪数，count 为总发数；返回并保存混音统计。"""
    print(f"[FourWeaponSFX] make_preview {name}")
    # 固定 -6dB 监听增益与 Cue 一致；不逐枪/逐段归一化，保留真实叠加结果。
    mix = np.zeros(round((count - 1) / rate * RATE) + max(map(len, voices)) + RATE // 2)
    for shot in range(count):  # shot 是当前发数，从 0 开始循环三个不同变体。
        start = round(shot / rate * RATE)
        voice = voices[shot % len(voices)]
        mix[start:start + len(voice)] += 0.5 * voice
    # 头尾增加静音只为试听播放，核心 WAV 无前置静音。
    write_wav(OUTPUT / "Previews" / f"{name}.wav", np.concatenate((np.zeros(RATE // 4), mix)))
    return {"file": f"Previews/{name}.wav", "shots_per_second": rate, "shots": count,
            "mix_peak_dbfs": round(20 * math.log10(float(np.max(np.abs(mix)))), 3)}


def main():
    """逐枪独立制作→磁盘复读→高射速试听→报告；任一无效样本或格式使执行失败。"""
    print("[FourWeaponSFX] main")
    # makers 将四类枪绑定到完全不同的配方函数；voices/records 分别存浮点监听样本及磁盘验证。
    makers = {"Pistol": pistol, "Rifle": rifle, "Shotgun": shotgun, "Sniper": sniper}
    voices = {}
    records = []
    for name, (duration, pitch, seed) in PROFILES.items():  # name/pitch/seed 为当前类别和确定性配置。
        voices[name] = []
        for variant in range(1, 4):  # variant 为稳定编号 1..3，重跑生成字节一致的内容。
            rng = np.random.default_rng(seed + variant * 997)
            time = np.arange(round(duration * RATE)) / RATE
            samples = finish(makers[name](rng, time), PEAK)
            path = OUTPUT / name / f"SW_{name}_Fire_{variant:02d}.wav"
            write_wav(path, samples)
            records.append(inspect(path, duration))
            voices[name].append(samples)
    assert len({record["sha256"] for record in records}) == 12
    # 受控低频与主体尾能量是客观保护线；不能替代耳机/小音箱的主观验收。
    for record in records:  # record 是单个已复读文件的审计数据。
        if "/Rifle/" in record["file"] or "/Shotgun/" in record["file"]:
            assert record["low_energy_after_80ms_ratio"] < 0.035, record
            assert record["energy_after_180ms_ratio"] < 0.001, record
    # 同峰值四枪对比按 Pistol→Rifle→Shotgun→Sniper，间隔 0.8s，音量统一乘0.5。
    comparison = np.concatenate([np.concatenate((voices[name][0] * 0.5, np.zeros(round(RATE * 0.8)))) for name in PROFILES])
    write_wav(OUTPUT / "Previews/FourWeapons_Compare.wav", np.concatenate((np.zeros(RATE // 4), comparison)))
    # 压力试听不修改游戏射速，覆盖文档要求的两个 Rifle RPM 和两个 Shotgun Hz。
    previews = [make_preview("Rifle_600RPM", voices["Rifle"], 10, 15),
                make_preview("Rifle_750RPM", voices["Rifle"], 12.5, 18),
                make_preview("Shotgun_4Hz", voices["Shotgun"], 4, 8),
                make_preview("Shotgun_6Hz", voices["Shotgun"], 6, 12)]
    # JSON 字段含义由武器音效文档维护；仅通过全部检查后写 passed=true。
    report = {"schema": 1, "generator": "generate_weapon_sfx.py", "passed": True, "variant_count": 3,
              "cue_volume": 0.5, "cue_gain_db_range": [-1.0, 0.5],
              "profiles": {name: {"duration_seconds": data[0], "pitch_range": data[1]} for name, data in PROFILES.items()},
              "files": records, "stress_previews": previews,
              "listening_status": "Requires human headphone and small-speaker review; numerical QA is not subjective acceptance"}
    (OUTPUT / "audio_validation.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("FOUR_WEAPON_SFX_GENERATION_SUCCESS files=12 previews=5")


if __name__ == "__main__":
    main()
