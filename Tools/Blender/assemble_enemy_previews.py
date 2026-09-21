"""把 Blender 实模、中立骨架和关节诊断帧排版；不生成或修饰几何细节。"""
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# 输入只读，输出位于同一预览目录；中文字体依赖 Windows 微软雅黑。
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Art/Enemies/Models"
PREVIEWS = SOURCE / "Previews"


def board(left, right, subtitle, entries):
    """left/right 为 PNG 路径，subtitle 为用途说明，entries 是已验证的骨数/三角形清单；返回可保存图像。"""
    print("[EnemyPreview] call board",flush=True)
    canvas = Image.new("RGB",(1440,740),"#14222b")  # 每次独立画布，动画帧不共享可变像素。
    draw = ImageDraw.Draw(canvas)
    title = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc",28)
    label = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc",21)
    draw.text((25,14),"BREACH / 敌人模型与骨骼",font=title,fill="#e1e9eb")
    for index,path in enumerate((left,right)):
        # 源图等比缩小；两个面板独立取景，不表示实际体型一样大。
        with Image.open(path) as source:
            canvas.paste(source.convert("RGB").resize((720,600),Image.Resampling.LANCZOS),(index*720,66))
        entry = entries[index]
        name = "撞击者 CHASER" if index==0 else "镇压核心 WARDEN"
        draw.text((index*720+22,77),f"{name}  ·  {len(entry['bones'])} 骨骼",font=label,fill="#ffc092")
    draw.text((25,688),subtitle,font=label,fill="#c0d1d6")
    return canvas


def main():
    """输出模型/骨架总览和 4 秒循环 GIF；缺帧立即报错，不回退概念图冒充实模。"""
    print("[EnemyPreview] call main",flush=True)
    entries = json.loads((SOURCE/"enemy_rig_manifest.json").read_text(encoding="utf-8"))["enemies"]
    board(PREVIEWS/"Chaser.png",PREVIEWS/"Warden.png","Blender 真实网格 · 刚性蒙皮 · 单根骨架 · 两面板独立取景",entries).save(PREVIEWS/"Breach_EnemyModels.png")
    board(PREVIEWS/"Chaser_Skeleton.png",PREVIEWS/"Warden_Skeleton.png","真实骨骼枢轴示意 · 橙色为骨段，银色为关节 · root 不参与视觉摆动",entries).save(PREVIEWS/"Breach_EnemySkeletons.png")
    frames = []  # 当前 GIF 的 24 帧，由真实 Blender FK 动作采样，不是图像形变。
    for index in range(24):
        frames.append(board(PREVIEWS/"Frames/Chaser"/f"{index:02d}.png",PREVIEWS/"Frames/Warden"/f"{index:02d}.png",
            "绑定诊断动作：双臂 / 推进器 / 核心护板开合 · 尚非正式战斗动画",entries))
    frames[0].save(PREVIEWS/"Breach_EnemyRigCheck.gif",save_all=True,append_images=frames[1:],duration=167,loop=0,optimize=True)
    print("BREACH_ENEMY_PREVIEWS_SUCCESS",flush=True)


if __name__ == "__main__":
    main()
