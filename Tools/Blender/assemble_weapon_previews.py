"""将 Blender 实模渲染排成带中文标签的比较图；不生成或修饰模型细节。"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# 只读取本项目 Blender 输出；中文字体使用 Windows 自带微软雅黑，输出可直接发给美术审阅。
ROOT = Path(__file__).resolve().parents[2]
PREVIEWS = ROOT / "Art/Weapons/Models/Previews"


def main():
    """同步生成 2×2 实模排版图；缺失任何原图即失败，不能回退为概念图。"""
    print("[WeaponPreview] call main", flush=True)
    # board/canvas 为本次排版内存对象；标题栏与格内标签独立于原始无字渲染。
    board = Image.new("RGB", (1680, 1220), "#101e27")
    canvas = ImageDraw.Draw(board)
    font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 27)
    small_font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 20)
    canvas.text((32, 20), "BREACH  /  四款武器 · Blender 实模", font=font, fill="#e2ecee")
    for index, (name, title) in enumerate((("Pistol", "01 手枪"), ("Rifle", "02 步枪"), ("Shotgun", "03 霰弹枪"), ("Sniper", "04 狙击枪"))):
        # x/y 是格子左上像素位置；原图等比缩小，保留整把武器及原有背景。
        x, y = (index % 2) * 840, 72+(index // 2)*564
        with Image.open(PREVIEWS / f"{name}.png") as source:
            board.paste(source.convert("RGB").resize((840, 540), Image.Resampling.LANCZOS), (x, y))
        canvas.text((x+24, y+12), title, font=small_font, fill="#aff5eb")
    canvas.text((32, 1180), "真实网格渲染  ·  灰白护板 / 深灰金属 / 青色标识  ·  UE 5.4 静态网格", font=small_font, fill="#b6c9d0")
    board.save(PREVIEWS / "Breach_Weapons_Models.png")
    print("WEAPON_PREVIEW_BOARD_SUCCESS", flush=True)


if __name__ == "__main__":
    main()
