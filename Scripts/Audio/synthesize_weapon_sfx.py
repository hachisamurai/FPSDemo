"""纯 Python 确定性合成：开火/怪物命中各三变体，输出真正 PCM24 WAV 和磁盘复读报告。"""
from pathlib import Path
import hashlib
import json
import math
import random
import wave

# 项目相对位置由脚本决定，不依赖调用者的工作目录；不写入 Content 或 .uasset。
PROJECT_ROOT = Path(__file__).resolve().parents[2]
# 用户指定 Sword 源目录保留；Fire/Hit 子目录区分用途，不暗示游戏改成近战。
OUTPUT_ROOT = PROJECT_ROOT / "SourceAssets/Audio/SFX/Weapons/Sword"
# PCM 规格：48000 samples/sec、0.4秒、单声道、24-bit little endian signed。
SAMPLE_RATE = 48000
DURATION = 0.4
FRAME_COUNT = int(SAMPLE_RATE * DURATION)
PCM_MAX = (1 << 23) - 1
# 峰值留余量：与 Cue 最大 +1.5dB 和多声部混音兼容；不是响度 LUFS 目标。
PEAK_DB = {"Fire": -9.0, "Hit": -12.0}


def synthesize_flesh(variant: int) -> list[float]:
    """variant 为 1..3；独立合成肉体闷击/湿润挤压，不复用开火扫频和金属模态，返回归一化浮点采样。"""
    print(f"[AudioSynthesis] synthesize_flesh variant={variant}")
    # rng 只服务当前肉体音效，固定种子保证资源可以重复生成。
    rng = random.Random(982451 + variant * 1543)
    # 低通状态分别限制皮肤拍击与组织挤压频带；body_noise 提供非乐音的低频重量。
    slap_low = 0.0
    tissue_low = 0.0
    body_noise = 0.0
    # body_hz 为受阻尼软组织冲击中心频率（Hz），不使用开火式向下高频扫频。
    body_hz = (92.0, 108.0, 81.0)[variant - 1]
    # pulses 为几次不规则组织挤压：(延迟秒、衰减秒、幅度)，每变体时序不同。
    pulses = [(0.002, 0.023, 1.0), (0.015 + variant * 0.002, 0.011, 0.70),
              (0.033 + variant * 0.003, 0.015, 0.48), (0.061 + variant * 0.004, 0.019, 0.22)]
    # samples 为当前0.4秒缓冲；绝大部分能量位于前0.12秒，余段保留柔和衰减。
    samples = []
    # index 为帧号，t 为秒；所有滤波与包络状态仅存在于同步生成过程。
    for index in range(FRAME_COUNT):
        t = index / SAMPLE_RATE
        # noise 为独立白噪声激励，经低通后产生柔软宽带接触声，不保留金属振铃。
        noise = rng.uniform(-1.0, 1.0)
        slap_low += (1.0 - math.exp(-math.tau * 3300.0 / SAMPLE_RATE)) * (noise - slap_low)
        tissue_low += (1.0 - math.exp(-math.tau * 1450.0 / SAMPLE_RATE)) * (noise - tissue_low)
        body_noise += (1.0 - math.exp(-math.tau * 190.0 / SAMPLE_RATE)) * (noise - body_noise)
        # slap 是皮肤瞬间拍击：快速起音、9ms衰减，避免形成持续破风声。
        slap = slap_low * (1.0 - math.exp(-t / 0.00045)) * math.exp(-t / 0.009)
        # compression 为湿润组织的离散挤压包络；delay/tau/level 分别为时移、衰减和权重。
        compression = 0.0
        for delay, tau, level in pulses:
            # age 为本次挤压经过秒数，未到触发时刻不贡献能量。
            age = t - delay
            if age > 0.0:
                compression += level * (1.0 - math.exp(-age / 0.0012)) * math.exp(-age / tau)
        # tissue 去除部分低频后作为湿润粗糙声，频带和包络与枪声独立。
        tissue = (tissue_low - body_noise) * compression
        # thud 的两个短阻尼低频分量带噪声调制，避免纯正弦鼓声或可辨识音高尾音。
        thud = (0.68 * math.sin(math.tau * body_hz * t) + 0.32 * math.sin(math.tau * body_hz * 1.37 * t))
        thud = (thud + 1.8 * body_noise) * (1.0 - math.exp(-t / 0.0009)) * math.exp(-t / 0.026)
        # settle 是低电平软组织回弹，只保留浑厚、无金属的短尾部。
        settle = body_noise * (1.0 - math.exp(-t / 0.008)) * math.exp(-t / 0.055)
        samples.append(0.80 * thud + 1.15 * slap + 2.40 * tissue + 0.25 * settle)
    # mean 去除直流；index/value 为边缘淡变使用的帧和值，首末端强制为零。
    mean = sum(samples) / FRAME_COUNT
    samples = [(value - mean) * min(1.0, index / 24.0, (FRAME_COUNT - 1 - index) / 1440.0)
               for index, value in enumerate(samples)]
    # gain 将肉体音效峰值归一化到 -12dBFS，为 Cue +1.5dB 保留余量，不硬削波。
    gain = 10.0 ** (PEAK_DB["Hit"] / 20.0) / max(abs(value) for value in samples)
    return [value * gain for value in samples]


def synthesize(kind: str, variant: int) -> list[float]:
    """kind 为 Fire/Hit，variant 为 1..3；返回 0.4 秒浮点波形，不持有外部资源。"""
    print(f"[AudioSynthesis] synthesize kind={kind} variant={variant}")
    # 命中采用独立肉体配方，完全绕过后续开火的破风/扫频/金属处理。
    if kind == "Hit":
        return synthesize_flesh(variant)
    # 固定不同种子可重复生成，噪声/基频/尾音均不同，不仅改名复制文件。
    rng = random.Random(73219 + variant * 997)
    # 一阶滤波状态属于本次音效；宽带噪声高频受限以避免刺耳和高频混叠。
    low_fast = 0.0
    low_slow = 0.0
    # 扫频相位逐样本积分，避免瞬变拼接时不连续。
    sweep_phase = 0.0
    body_phase = 0.0
    # 三个变体的金属共振基频（Hz），非谐整数比产生轻微金属质感。
    ring_frequency = (1580.0, 1840.0, 1370.0)[variant - 1]
    # 振荡器初相位为确定性随机值；用于区分三个共振模式。
    phases = [rng.uniform(0.0, math.tau) for _ in range(3)]
    # 未归一化浮点采样值，仅在本函数中积累。
    samples = []
    # index 为 0..19199 的帧索引，t 为秒。
    for index in range(FRAME_COUNT):
        t = index / SAMPLE_RATE
        # 开火保留原有急促破风与爆发，修改命中质感不改变这条配方。
        noise = rng.uniform(-1.0, 1.0)
        cutoff = 6200.0 * math.exp(-t * 9.0) + 500.0
        fast_alpha = 1.0 - math.exp(-math.tau * cutoff / SAMPLE_RATE)
        slow_alpha = 1.0 - math.exp(-math.tau * 300.0 / SAMPLE_RATE)
        low_fast += fast_alpha * (noise - low_fast)
        low_slow += slow_alpha * (noise - low_slow)
        # 1.5ms 攻击、40~70ms 衰减的带通噪声提供快速破风；后半段不持续嘶鸣。
        air_envelope = (1.0 - math.exp(-t / 0.0015)) * math.exp(-t / (0.041 + variant * 0.006))
        air = (low_fast - low_slow) * air_envelope
        # 高频快速下降扫频负责“嗖”，不做真实枪械录音冒充。
        sweep_hz = 450.0 + (2600.0 + variant * 190.0) * math.exp(-t * 43.0)
        sweep_phase += math.tau * sweep_hz / SAMPLE_RATE
        sweep = math.sin(sweep_phase) * math.exp(-t / 0.026) * (1.0 - math.exp(-t / 0.001))
        # 开火的短促低频枪身冲击，保留已有音色与数值。
        body_hz = 105.0 + 200.0 * math.exp(-t * 65.0)
        body_phase += math.tau * body_hz / SAMPLE_RATE
        body = math.sin(body_phase) * (1.0 - math.exp(-t / 0.0008)) * math.exp(-t / 0.028)
        # 金属尾音在瞬态之后渐入，三个非整数倍模态快速衰减，避免尖锐长鸣。
        metal = 0.0
        # mode 为三个共振模态索引，ratio 为相对基频，不产生循环资源。
        for mode, ratio in enumerate((1.0, 1.483, 2.071)):
            metal += math.sin(math.tau * ring_frequency * ratio * t + phases[mode]) * math.exp(-t / (0.085 + 0.022 * mode)) / (mode + 1)
        metal *= (1.0 - math.exp(-t / 0.009)) * 0.085
        # 前后淡变避免非零端点导致切割爆音；末尾 30ms 平滑到绝对零。
        fade_in = min(1.0, t / 0.0007)
        fade_out = math.sin(min(1.0, (DURATION - 1.0 / SAMPLE_RATE - t) / 0.03) * math.pi / 2.0) ** 2
        value = (1.6 * air + 0.20 * sweep + 0.55 * body + metal) * fade_in * fade_out
        samples.append(value)
    # 去直流后再次用边缘淡变保持头尾为零；归一化不使用削波/硬限制器。
    mean = sum(samples) / FRAME_COUNT
    samples = [(value - mean) * min(1.0, index / 48.0, (FRAME_COUNT - 1 - index) / 480.0) for index, value in enumerate(samples)]
    # 每用途固定峰值，变体间一致，不被随机波形峰值影响响度过大。
    peak = max(abs(value) for value in samples)
    gain = 10.0 ** (PEAK_DB[kind] / 20.0) / peak
    return [value * gain for value in samples]


def write_pcm24(path: Path, samples: list[float]) -> None:
    """path 为项目 SourceAssets 下输出文件；samples 必须有限且绝对值 <1，保存 PCM24。"""
    print(f"[AudioSynthesis] write_pcm24 {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    # 确定性 TPDF dither 只有约 1 LSB，避免量化相关噪声；头尾保持零。
    rng = random.Random(path.name)
    # payload 为当前 WAV 的交错 PCM 字节缓冲；单声道每帧恰好三个字节。
    payload = bytearray()
    # index/value 为当前输出帧和值，encoded 为有符号24位整数。
    for index, value in enumerate(samples):
        if not math.isfinite(value) or abs(value) >= 1.0:
            raise ValueError("Non-finite or clipped source sample")
        # dither 以量化整数的 LSB 为单位，禁止污染明确静音的首末帧。
        dither = rng.random() - rng.random() if 0 < index < len(samples) - 1 else 0.0
        encoded = round(value * PCM_MAX + dither)
        payload.extend(encoded.to_bytes(3, "little", signed=True))
    # writer 仅在 with 生命周期内持有文件，标准库自动写 RIFF/WAVE/fmt/data 块。
    with wave.open(str(path), "wb") as writer:
        writer.setparams((1, 3, SAMPLE_RATE, len(samples), "NONE", "not compressed"))
        writer.writeframes(payload)


def inspect_wav(path: Path) -> dict:
    """path 为磁盘 WAV；重新解码并校验规格、峰值、削波和末端，不依赖生成时的数组。"""
    print(f"[AudioSynthesis] inspect_wav {path}")
    # reader 在此块内持有磁盘流；spec 是实际 WAV 头，不采用预期参数冒充检测结果。
    with wave.open(str(path), "rb") as reader:
        spec = reader.getparams()
        payload = reader.readframes(spec.nframes)
    if (spec.nchannels, spec.sampwidth, spec.framerate, spec.nframes, spec.comptype) != (1, 3, SAMPLE_RATE, FRAME_COUNT, "NONE"):
        raise ValueError(f"Unexpected WAV format: {spec}")
    # 解码24-bit有符号采样；sample_peak/RMS/DC 统一按满幅 2^23 计算。
    integers = [int.from_bytes(payload[index:index+3], "little", signed=True) for index in range(0, len(payload), 3)]
    decoded = [value / float(1 << 23) for value in integers]
    sample_peak = max(abs(value) for value in decoded)
    rms = math.sqrt(sum(value * value for value in decoded) / len(decoded))
    clipped = sum(value <= -(1 << 23) or value >= PCM_MAX for value in integers)
    # 4x/16-tap windowed sinc 重建峰值为附加估算（不是认证 BS.1770 true-peak 仪表）。
    reconstructed_peak = sample_peak
    # fraction 为相邻原始样本间的三个插值相位，以一帧为单位。
    for fraction in (0.25, 0.5, 0.75):
        # weights 保存分数采样相位的固定 Lanczos 核；总和归一化保持 DC 增益。
        weights = []
        # offset 为邻近样本偏移，distance 为核中心到该样本的分数帧距离。
        for offset in range(-7, 9):
            distance = fraction - offset
            weights.append(math.sin(math.pi*distance)/(math.pi*distance) * math.sin(math.pi*distance/8)/(math.pi*distance/8))
        # weight_sum 修正有限核的直流增益；index 仅遍历拥有完整16点邻域的原始帧。
        weight_sum = sum(weights)
        for index in range(7, len(decoded)-8):
            reconstructed_peak = max(reconstructed_peak, abs(sum(decoded[index+offset]*weights[offset+7] for offset in range(-7,9))/weight_sum))
    if clipped or reconstructed_peak >= 1.0 or integers[0] != 0 or integers[-1] != 0:
        raise ValueError(f"Clipping or nonzero boundaries: {path}")
    return {
        "file": path.relative_to(PROJECT_ROOT).as_posix(), "sample_rate_hz": spec.framerate,
        "bit_depth": spec.sampwidth*8, "channels": spec.nchannels, "frames": spec.nframes,
        "duration_seconds": spec.nframes/spec.framerate, "encoding": "PCM signed 24-bit little-endian",
        "sample_peak_dbfs": round(20*math.log10(sample_peak), 4),
        "rms_dbfs": round(20*math.log10(rms), 4), "dc_offset": sum(decoded)/len(decoded),
        "estimated_4x_peak_dbfs": round(20*math.log10(reconstructed_peak), 4),
        "clipped_samples": clipped, "first_sample": integers[0], "last_sample": integers[-1],
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def main() -> None:
    """生成六个音效，磁盘复读合格后写报告；任一失败以非零退出码停止后续导入。"""
    print("[AudioSynthesis] main")
    # records 为六条磁盘检查结果，供 UE 导入脚本校验哈希与规格。
    records = []
    # kind/variant 枚举两个用途与三个变体，确定可重复的源文件命名和种子。
    for kind in ("Fire", "Hit"):
        for variant in range(1, 4):
            # path 的名称/类别稳定，重复运行确定性覆盖本脚本的同名原始音效。
            path = OUTPUT_ROOT / kind / f"SW_Sword_{kind}_{variant:02d}.wav"
            write_pcm24(path, synthesize(kind, variant))
            records.append(inspect_wav(path))
    if len({record["sha256"] for record in records}) != 6:
        raise ValueError("Variants are not unique")
    # 报告包含边界说明，所有 JSON 配置字段解释见 Documentation/Audio/武器音效.md。
    report = {"schema": 1, "generator": "synthesize_weapon_sfx.py", "files": records,
              "peak_method": "disk PCM sample peak; supplementary 4x 16-tap windowed-sinc estimate",
              "cue_pitch_range": [0.95, 1.05], "cue_gain_db_range": [-1.5, 1.5], "passed": True}
    (OUTPUT_ROOT / "audio_validation.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
