"""按真实机械动画manifest合成8套换弹试听；只写Previews，不覆盖21个源音效或玩法资源。"""
import array
import hashlib
import html
import json
import math
import sys
import wave
from pathlib import Path

# 路径从项目内脚本位置推导；声音和机械阶段分别读取其现有源清单，避免维护第二套时间表。
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Art/Audio/Reload"
ANIMATION_MANIFEST = ROOT / "Art/Weapons/Animations/weapon_animation_manifest.json"
OUTPUT = SOURCE / "Previews"
# 试听输出与源PCM一致；末尾增加0.3秒，保留最后一下机械音衰减，不改变实际换弹时长。
SAMPLE_RATE = 48000
TAIL_SECONDS = .3
# 展示名称仅用于中文页面，资源文件仍保持稳定英文型号和动作后缀。
DISPLAY_NAMES = {"Pistol": "手枪", "Rifle": "步枪", "Shotgun": "散弹枪", "Sniper": "狙击枪"}
EVENT_LABELS = {"MagOut": "拔出弹匣", "MagIn": "插入弹匣", "MagSeat": "压实弹匣",
                "ChargingHandle": "操作拉机柄", "BoltOpen": "枪栓打开", "BoltClose": "枪栓关闭",
                "SlideRelease": "释放套筒"}


def read_source(path):
    """path为源WAV绝对路径；验证48kHz单声道PCM16并返回归一化浮点样本，不修改文件。"""
    print(f"[CALL] ReloadAudioPreview.read_source {path.name}", flush=True)
    if not path.is_file():
        raise FileNotFoundError(f"动画事件没有对应音效: {path}")
    with wave.open(str(path), "rb") as source_wave:
        # metadata记录源PCM格式；禁止隐式重采样掩盖制作管线格式不一致。
        metadata = (source_wave.getnchannels(), source_wave.getsampwidth(), source_wave.getframerate(), source_wave.getcomptype())
        if metadata != (1, 2, SAMPLE_RATE, "NONE"):
            raise ValueError(f"Unsupported source PCM {path.name}: {metadata}")
        # pcm在当前函数拥有；Windows为小端，byteswap分支保持跨平台重建一致。
        pcm = array.array("h", source_wave.readframes(source_wave.getnframes()))
        if sys.byteorder != "little":
            pcm.byteswap()
    if not pcm:
        raise ValueError(f"Empty source PCM: {path.name}")
    return [sample / 32768.0 for sample in pcm]


def write_preview(path, samples):
    """path为专属试听输出；samples为合成浮点音轨，峰值超过0.95才统一衰减，返回实际声学检查。"""
    print(f"[CALL] ReloadAudioPreview.write_preview {path.name}", flush=True)
    # peak/gain保持常规试听原始响度，仅在重叠导致超阈值时留5%数字余量。
    peak = max(abs(sample) for sample in samples)
    if peak <= 1e-8:
        raise ValueError(f"Silent preview: {path.name}")
    gain = min(1.0, .95 / peak)
    pcm = array.array("h", (round(sample * gain * 32767) for sample in samples))
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(path), "wb") as output_wave:
        output_wave.setnchannels(1)
        output_wave.setsampwidth(2)
        output_wave.setframerate(SAMPLE_RATE)
        output_wave.writeframes(pcm.tobytes())
    # 重读实际输出而不是只验证工作缓冲，保证页面播放的是正确长度且未削波的文件。
    written = read_source(path)
    if len(written) != len(samples) or max(abs(sample) for sample in written) >= 1:
        raise RuntimeError(f"PCM verification failed: {path.name}")
    rms = math.sqrt(sum(sample * sample for sample in written) / len(written))
    return {"peak_dbfs": 20 * math.log10(max(abs(sample) for sample in written)),
            "rms_dbfs": 20 * math.log10(rms), "mix_gain": gain, "clipped_samples": 0}


def build_track(model, clip):
    """model为枪型，clip为实际机械动画条目；将每个声音放在phase*seconds，返回试听与事件校验元数据。"""
    print(f"[CALL] ReloadAudioPreview.build_track {model}/{clip['variant']}", flush=True)
    # duration/frame_count是含0.3秒尾部的试听长度；不写回游戏ReloadSeconds。
    duration = clip["seconds"] + TAIL_SECONDS
    frame_count = round(duration * SAMPLE_RATE)
    mix = [0.0] * frame_count
    events = []
    previous_start = None  # 只属于当前音轨，检查事件顺序和相邻起音间隔。
    previous_end = 0.0  # 上一声音尾部秒位置，用于记录允许的自然尾音重叠。
    for event in clip["sound_events"]:
        # event_name/phase/time来自动画权威清单；源文件名称只是稳定映射，不另行猜测时间。
        event_name = event["name"]
        phase = event["phase"]
        if event_name not in EVENT_LABELS or not 0 <= phase <= 1:
            raise ValueError(f"Invalid animation sound event: {model}/{event}")
        time = phase * clip["seconds"]
        if previous_start is not None and time <= previous_start:
            raise ValueError(f"Nonascending sound events: {model}/{clip['variant']}/{event_name}")
        source_path = SOURCE / f"SW_Reload_{model}_{event_name}.wav"
        source_samples = read_source(source_path)
        start_sample = round(time * SAMPLE_RATE)
        if start_sample + len(source_samples) > len(mix):
            raise ValueError(f"Preview tail insufficient: {model}/{event_name}")
        for index, sample in enumerate(source_samples):  # 仅离线PCM叠加，保留所有自然尾音而不截断或平移关键事件。
            mix[start_sample + index] += sample
        events.append({"name": event_name, "source": source_path.name, "phase": phase,
                       "seconds": time, "sample": start_sample, "source_seconds": len(source_samples) / SAMPLE_RATE,
                       "interval_from_previous_seconds": None if previous_start is None else time - previous_start,
                       "tail_overlap_previous_seconds": max(0.0, previous_end - time),
                       "source_sha256": hashlib.sha256(source_path.read_bytes()).hexdigest()})
        previous_start = time
        previous_end = time + len(source_samples) / SAMPLE_RATE
    if not events:
        raise ValueError(f"No events: {model}/{clip['variant']}")
    # path以动作版本区分普通/空仓，浏览器通过同目录相对URL播放。
    path = OUTPUT / f"{model}_{clip['variant']}.wav"
    statistics = write_preview(path, mix)
    print(f"RELOAD_AUDIO_PREVIEW_VALID {path.name} events={len(events)} peak={statistics['peak_dbfs']:.2f}dBFS", flush=True)
    return {"model": model, "variant": clip["variant"], "file": path.name,
            "reload_seconds": clip["seconds"], "preview_seconds": frame_count / SAMPLE_RATE,
            "sample_rate": SAMPLE_RATE, "channels": 1, "events": events, **statistics}


def write_html(tracks):
    """tracks为八套已校验试听；生成可直接打开的离线HTML，不需要服务器、网络或浏览器权限。"""
    print("[CALL] ReloadAudioPreview.write_html", flush=True)
    # cards/sections仅在页面构建时存在；浏览器音频controls为原生交互，不加载脚本或外部资源。
    cards = []
    for model, display in DISPLAY_NAMES.items():
        sections = []
        for track in tracks:
            if track["model"] != model:
                continue
            # title/event_items为展示文本，所有动态数据转义；明确图片是几何预览而非手臂运行画面。
            title = "普通换弹" if track["variant"] == "Tactical" else "空仓换弹"
            event_items = "".join(f'<li><span>{html.escape(EVENT_LABELS[event["name"]])}</span><time>{event["seconds"]:.3f}s</time></li>' for event in track["events"])
            sections.append(f'''<section class="track"><div class="track-title"><h3>{title}</h3><span>{track["reload_seconds"]:.1f}s 动作 · {track["preview_seconds"]:.1f}s 试听</span></div>
<audio controls preload="metadata" src="{html.escape(track['file'])}">浏览器不支持音频控件。</audio>
<ul class="events">{event_items}</ul><a class="download" href="{html.escape(track['file'])}" download>下载 WAV</a></section>''')
        cards.append(f'''<article class="weapon"><div class="visual"><img src="../../../Weapons/Animations/Previews/{model}_Neutral.png" alt="{display}真实几何中立姿势预览"><div class="weapon-label"><span>{model.upper()}</span><h2>{display}</h2></div></div>{''.join(sections)}</article>''')
    # document只使用本地图片与WAV；说明时序用途和0.3s余音，让用户不会误认这些试听已在UE中验收。
    document = '''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Breach 四枪换弹试听</title><style>
:root{color-scheme:dark;--bg:#0c141a;--panel:#14212a;--line:#2c3f49;--text:#e5edef;--muted:#9daeb8;--accent:#63dfce}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.55 system-ui,"Microsoft YaHei",sans-serif}main{max-width:1400px;margin:auto;padding:42px 28px 60px}header{max-width:1000px;margin-bottom:30px}header .eyebrow{letter-spacing:.2em;color:var(--accent);font-size:12px}h1{font-size:32px;line-height:1.25;margin:12px 0 14px}header p{color:var(--muted);margin:8px 0}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:22px}.weapon{background:var(--panel);border:1px solid var(--line);border-radius:16px;overflow:hidden}.visual{position:relative;background:#36464d}.visual img{display:block;width:100%;aspect-ratio:1200/760;object-fit:cover}.weapon-label{position:absolute;top:18px;left:20px;text-shadow:0 2px 8px #000}.weapon-label span{color:var(--accent);letter-spacing:.14em;font-size:11px}.weapon-label h2{margin:2px 0;font-size:25px}.track{padding:19px 22px;border-top:1px solid var(--line)}.track-title{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:12px}h3{font-size:16px;margin:0}.track-title span{font-size:12px;color:var(--muted)}audio{width:100%;height:38px}.events{list-style:none;padding:0;margin:14px 0 10px;display:flex;gap:7px;flex-wrap:wrap}.events li{background:#0d1921;border:1px solid #2c404c;border-radius:6px;padding:5px 8px;display:flex;gap:9px;font-size:11px}.events time{color:var(--accent);font-variant-numeric:tabular-nums}.download{font-size:12px;color:var(--accent);text-decoration:none}.download:hover{text-decoration:underline}footer{margin-top:28px;color:var(--muted);font-size:12px}footer a{color:var(--accent)}@media(max-width:850px){.grid{grid-template-columns:1fr}main{padding:26px 14px}h1{font-size:26px}.track{padding:16px}.track-title{align-items:start;flex-direction:column;gap:3px}}
</style></head><body><main><header><div class="eyebrow">BREACH · RELOAD AUDIO</div><h1>四枪换弹 · 八套时序试听</h1><p>按真实机械动画的事件时刻合成：拔匣、插匣、压实与空仓机构操作。原音效保持不变，末尾保留 0.3 秒余音。</p><p>图片是 Blender 真实枪械几何预览；本页用于试听节奏，不代表手臂动画或 UE 内运行验收。可先播放普通版本，再对比空仓操作。</p></header><div class="grid">''' + "".join(cards) + '''</div><footer>48 kHz · 单声道 · PCM 16-bit · 所有八套文件已检查事件来源、相邻时间、输出长度与削波。<br><a href="audio_preview_manifest.json">查看时序与峰值检查数据</a> · 允许相邻机械声自然尾音重叠，保持动画事件时间不变。</footer></main></body></html>'''
    (OUTPUT / "index.html").write_text(document, encoding="utf-8")


def main():
    """入口只写Art/Audio/Reload/Previews；缺失源音效/非法时序/削波验证失败时抛异常，禁止伪报成功。"""
    print("[CALL] ReloadAudioPreview.main", flush=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    # animation/tracks分别为只读权威清单和本轮生成记录；必须恰好四型号、各普通/空仓两套。
    animation = json.loads(ANIMATION_MANIFEST.read_text(encoding="utf-8"))
    tracks = []
    for weapon in animation["weapons"]:
        for clip in weapon["clips"]:
            tracks.append(build_track(weapon["model"], clip))
    if len(tracks) != 8 or {(track["model"], track["variant"]) for track in tracks} != {(model, variant) for model in DISPLAY_NAMES for variant in ("Tactical", "Empty")}:
        raise ValueError("Expected exactly four weapons and eight reload previews")
    (OUTPUT / "audio_preview_manifest.json").write_text(json.dumps({"version": 1, "tail_seconds": TAIL_SECONDS,
        "animation_source": "Art/Weapons/Animations/weapon_animation_manifest.json", "tracks": tracks}, ensure_ascii=False, indent=2), encoding="utf-8")
    write_html(tracks)
    print("RELOAD_AUDIO_PREVIEWS_SUCCESS tracks=8 clipping=0", flush=True)


if __name__ == "__main__":
    main()
