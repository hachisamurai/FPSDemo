"""把UE真实RHI逐帧截图与同游戏时间轴的原创音轨合成预览；不改变动画、截图或声音源资源。"""
import csv
import html
import json
import shutil
import subprocess
import sys
from pathlib import Path

# 所有输入/输出限定在当前项目；帧间隔使用实测TSV，截图低于30FPS时不得擅自加速动作。
ROOT = Path(__file__).resolve().parents[2]
FRAMES = ROOT / "Saved/Screenshots/WeaponAnimation/Frames"
AUDIO = ROOT / "Art/Audio/Reload/Previews"
OUTPUT = ROOT / "Art/Weapons/Animations/VideoPreviews"
# 显示名称只用于预览页；资源仍采用四枪统一英文型号，便于追溯对应UE动画。
NAMES = {"Pistol": "手枪", "Rifle": "步枪", "Shotgun": "散弹枪", "Sniper": "狙击枪"}


def encoder_path():
    """优先使用已有ffmpeg；否则读取项目Saved临时Python依赖，不联网且不修改全局环境。"""
    print("[CALL] ReloadVideoPreview.encoder_path", flush=True)
    # resolved只在工具运行时使用；本仓库不携带ffmpeg二进制。
    resolved = shutil.which("ffmpeg")
    if resolved:
        return resolved
    sys.path.insert(0, str(ROOT / "Saved/Tools/FPPreviewPython"))
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()


def encode_clip(encoder, model, variant):
    """encoder为可执行文件，model/variant标识真实用例；返回实际帧数、游戏时间与输出路径。"""
    print(f"[CALL] ReloadVideoPreview.encode_clip {model}/{variant}", flush=True)
    # stem与专项录制的文件名一致；只使用TSV列出的截图，不混入之前更长的录制残帧。
    stem = f"{model}_{variant}"
    with (FRAMES / f"{stem}_timing.tsv").open(encoding="utf-8-sig", newline="") as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    if len(rows) < 3:
        raise ValueError(f"Not enough real RHI frames: {stem}")
    # times是每张实际截图相对StartReload的游戏秒，不是视频编码器估算的帧率。
    times = [float(row["relative_game_seconds"]) for row in rows]
    if times[0] < 0 or any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError(f"Nonascending frame clock: {stem}")
    # concat清单只写入本工具独占输出目录；ffmpeg参数通过argv传递，不使用shell字符串拼接。
    lines = ["ffconcat version 1.0"]
    for index, row in enumerate(rows):
        # path先检查属于录制目录；文件名来自本地测试元数据，不允许越出目录读其他资源。
        path = (FRAMES / row["frame_filename"]).resolve()
        if path.parent != FRAMES.resolve() or not path.is_file():
            raise ValueError(f"Missing or invalid frame: {path}")
        # 首帧补齐StartReload到第一次截图的小间隔，最后留0.30秒便于看清归位；中间完全遵循游戏时间。
        duration = (times[index + 1] - (0.0 if index == 0 else times[index])) if index + 1 < len(rows) else .30
        lines.extend(["file '" + path.as_posix().replace("'", "'\\''") + "'", f"duration {duration:.6f}"])
    lines.append(lines[-2])  # concat末帧必须重复一项才会应用上一duration，不创造额外动作。
    listing = OUTPUT / f"{stem}.ffconcat"
    listing.write_text("\n".join(lines) + "\n", encoding="utf-8")
    target = OUTPUT / f"{stem}.mp4"
    # 混合音轨依据同一manifest的phase*ReloadSeconds拼接，是同步试听而不是声卡录音。
    command = [encoder, "-hide_banner", "-loglevel", "error", "-y", "-safe", "0", "-f", "concat", "-i", str(listing),
               "-i", str(AUDIO / f"{stem}.wav"), "-vf", "fps=30", "-c:v", "libx264", "-preset", "medium", "-crf", "21",
               "-pix_fmt", "yuv420p", "-af", "volume=0.75,apad", "-c:a", "aac", "-b:a", "160k", "-shortest",
               "-movflags", "+faststart", str(target)]
    subprocess.run(command, check=True)
    if target.stat().st_size < 10000:
        raise RuntimeError(f"Unexpected empty video: {target}")
    print(f"RELOAD_VIDEO_READY {stem} frames={len(rows)} game_seconds={times[-1]:.3f}", flush=True)
    return {"model": model, "variant": variant, "file": target.name, "captured_frames": len(rows),
            "first_game_seconds": times[0], "last_game_seconds": times[-1], "bytes": target.stat().st_size,
            "audio_source": "manifest-timed original mechanical mix, gain 0.75; not loopback recording"}


def main():
    """入口为手动验证完成后的离线预览构建；缺帧、缺音轨或编码失败立即报错，不发布残缺页面。"""
    print("[CALL] ReloadVideoPreview.main", flush=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    encoder = encoder_path()  # 仅本进程使用的工具路径。
    clips = []  # 八条已完成编码的清单，所有视频成功后才生成索引。
    for model in NAMES:
        for variant in ("Tactical", "Empty"):
            clips.append(encode_clip(encoder, model, variant))
    # 页面完全离线，没有外部脚本；用户可直接播放真实UE画面，音轨拼接来源在页面明确说明。
    cards = []
    for clip in clips:
        label = NAMES[clip["model"]] + (" · 普通换弹" if clip["variant"] == "Tactical" else " · 空仓换弹")
        cards.append(f'<article><h2>{html.escape(label)}</h2><video controls preload="metadata" src="{clip["file"]}"></video></article>')
    page = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>BREACH · 换弹预览</title>
<style>body{background:#101a21;color:#dbe8ec;font:16px system-ui;margin:32px auto;max-width:1280px;padding:0 24px}
h1{letter-spacing:.2em;color:#82ddd5}p{line-height:1.8;color:#acbec7}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(420px,1fr));gap:24px}
article{background:#182832;border:1px solid #29444f;padding:14px;border-radius:12px}h2{font-size:18px;font-weight:500}video{width:100%;border-radius:6px}</style>
<h1>BREACH / RELOAD</h1><p>左手转腕与握匣修正版：张手接近 → 转腕握匣 → 抽匣 → 插匣 → 松手回握；空仓追加机械操作。</p><p>四种枪械 · 普通与空仓换弹。画面来自UE实际运行录帧，按每帧游戏时间还原动作速度；
音轨由游戏使用的原创机械声按同一动作时间表合成，便于核对拔匣、插匣与拉栓时点，并非声卡实录。</p><main>'''
    (OUTPUT / "index.html").write_text(page + "\n".join(cards) + "</main></html>", encoding="utf-8")
    (OUTPUT / "video_preview_manifest.json").write_text(json.dumps(clips, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("RELOAD_VIDEO_PREVIEWS_SUCCESS clips=8", flush=True)


if __name__ == "__main__":
    main()
