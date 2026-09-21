"""把最终UE只读导出动作加入原共享蒙皮工作台，并对其真实变形网格逐帧检查枪托穿入。"""
import importlib.util
import json
import sys
from pathlib import Path
import bpy
import numpy as np
from mathutils import Matrix

# 仅改独立美术工作台；FinalUE、CurrentUE快照和Content均为只读输入。
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Art/Player/Animations"
SPEC = importlib.util.spec_from_file_location("skin", ROOT / "Tools/Blender/validate_fp_reload_skin.py")
SKIN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SKIN)
WB = SKIN.WB


@WB.traced
def finalize_view_and_gallery():
    """保存时直接显示最终UE步枪持握，视图对准真实几何，并生成可点击的最终动作静帧索引。"""
    # 相机与编辑视图仅决定用户打开时的可见区域，不参与皮肤/枪托实体计算。
    manifest = json.loads((OUT / "workbench_manifest.json").read_text(encoding="utf-8"))
    mapping = Matrix(next(item["ue_to_blender"] for item in manifest["models"] if item["model"] == "Rifle"))
    groups = {model: bpy.data.collections[f"FP_{model}"] for model in WB.MODELS}
    WB.show_group(groups, "Rifle")
    arms = bpy.data.objects["Arms_Rifle"]
    gun = bpy.data.objects["Gun_Rifle"]
    WB.assign_action(arms, bpy.data.actions["FinalUE_Rifle_Idle"])
    WB.assign_action(gun, None)
    for joint in gun.pose.bones:
        joint.matrix_basis = Matrix.Identity(4)
    WB.set_frame(WB.action_frame(arms.animation_data.action, 0))
    camera = bpy.data.objects["WorkbenchCamera"]
    target = gun.matrix_world.translation + mapping.to_3x3() @ WB.Vector((-8, 0, -8))
    camera.location = target + mapping.to_3x3() @ WB.Vector((-42, 150, 38))
    camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 120
    camera.data.clip_start = .5
    bpy.context.scene.camera = camera
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                area.spaces.active.region_3d.view_location = target
                area.spaces.active.region_3d.view_rotation = camera.rotation_euler.to_quaternion()
                area.spaces.active.region_3d.view_distance = 100
                area.spaces.active.region_3d.view_perspective = "ORTHO"
                area.spaces.active.shading.type = "SOLID"
                area.spaces.active.shading.color_type = "MATERIAL"
    bpy.ops.object.select_all(action="DESELECT")
    arms.select_set(True)
    bpy.context.view_layer.objects.active = arms
    # 静态索引只链接真实FinalUE图；旧/候选图仍在目录中，避免对照资产冒充最终交付。
    html = ['<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>BREACH 四枪联合工作台</title>',
            '<style>body{margin:32px auto;padding:0 20px;max-width:1500px;background:#14212b;color:#e6eef1;font:16px system-ui}h1{color:#72d9d2}a{color:#72d9d2}section{margin-top:36px}.grid{display:grid;grid-template-columns:repeat(2,minmax(280px,1fr));gap:16px}figure{margin:0;background:#20313d;border-radius:8px;overflow:hidden}img{width:100%;display:block}figcaption{padding:12px;color:#c7d8df}</style>',
            '<h1>BREACH · 四枪联合工作台</h1><p>最终 UE 导出动作 × 原始 160 骨骼 / 21570 顶点蒙皮 × 真实枪械零件。点击图片查看原图。</p>',
            '<p>以下是 Blender 几何审查视图，使用诊断材质。第一人称参考投影不含运行时 AnimGraph IK、镜头摆动或 UE 光照；实际游戏效果以 UE 录制为准。</p><p><a href="../Breach_FP_Reload_Workbench.blend">可编辑 Blender 工作台</a> · <a href="../WORKBENCH_README.md">工作台说明</a> · <a href="../workbench_final_ue_skin_diagnostic.json">最终真实蒙皮检查</a></p>']
    for model in WB.MODELS:
        html.append(f'<section><h2>{model}</h2><div class="grid">')
        for stage, label in (("FinalGrip", "持握 · Idle"), ("FinalMagazine", "取匣 · Tactical 32%"), ("FinalMechanism", "机械操作 · Empty 74%")):
            for view, view_label in (("Side", "侧视"), ("FirstPerson", "第一人称参考")):
                file = f"{model}_{stage}_{view}.png"
                html.append(f'<figure><a href="{file}"><img src="{file}" alt="{model} {label} {view_label}" loading="lazy"></a><figcaption>{label} / {view_label}</figcaption></figure>')
        html.append('</div></section>')
    html.append('</html>')
    (OUT / "WorkbenchPreviews/index.html").write_text("\n".join(html), encoding="utf-8")


@WB.traced
def import_verified_action(entry, snapshot="FinalUE", prefix="FinalUE"):
    """entry为UE清单项，snapshot/prefix区分历史与新快照；确认原蒙皮/骨架相同，禁止静默重定向。"""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(OUT / snapshot / entry["fbx"]["file"]), use_anim=True)
    created = [obj for obj in bpy.data.objects if obj not in before]
    rig = next(obj for obj in created if obj.type == "ARMATURE")
    mesh = next(obj for obj in created if obj.type == "MESH")
    original_rig = bpy.data.objects[f"Arms_{entry['model']}"]
    original_mesh = bpy.data.objects[f"SK_UE_Arms_{entry['model']}"]
    if set(rig.data.bones.keys()) != set(original_rig.data.bones.keys()):
        raise RuntimeError("Final UE skeleton differs")
    # 参考顶点与绑定权重逐项一致才复用共享网格；这里校验的是实际FBX数据，不仅网格名称。
    if len(mesh.data.vertices) != len(original_mesh.data.vertices):
        raise RuntimeError("Final UE skin vertex count differs")
    vertex_error = max((a.co - b.co).length for a, b in zip(mesh.data.vertices, original_mesh.data.vertices))
    bone_error = max(max(abs(a - b) for row_a, row_b in zip(rig.data.bones[name].matrix_local, original_rig.data.bones[name].matrix_local) for a, b in zip(row_a, row_b)) for name in rig.data.bones.keys())
    groups = {item.index: item.name for item in mesh.vertex_groups}
    original_groups = {item.index: item.name for item in original_mesh.vertex_groups}
    if groups != original_groups or vertex_error > .001 or bone_error > .001:
        raise RuntimeError(f"Final UE reference differs vertices={vertex_error} bones={bone_error}")
    weight_error = 0.0
    for vertex, original in zip(mesh.data.vertices, original_mesh.data.vertices):
        influences = {group.group: group.weight for group in vertex.groups}
        original_influences = {group.group: group.weight for group in original.groups}
        if influences.keys() != original_influences.keys():
            raise RuntimeError("Final UE vertex influence set differs")
        weight_error = max(weight_error, max((abs(influences[key] - original_influences[key]) for key in influences), default=0))
    if weight_error > 1e-5:
        raise RuntimeError(f"Final UE skin weights differ {weight_error}")
    action = rig.animation_data.action
    action.name = f"{prefix}_{entry['model']}_{entry['variant']}"
    action.use_fake_user = True
    for obj in created:
        bpy.data.objects.remove(obj, do_unlink=True)
    return action, {"action": action.name, "vertices_max_error_cm": vertex_error, "reference_bone_matrix_max_error": bone_error, "weights_max_error": weight_error}


@WB.traced
def main():
    """最终12段UE实际动作加入工作台，逐帧检查三长枪并生成四枪最终阶段预览。"""
    bpy.ops.wm.open_mainfile(filepath=str(OUT / "Breach_FP_Reload_Workbench.blend"))
    source = json.loads((OUT / "FinalUE/current_ue_workbench_manifest.json").read_text(encoding="utf-8"))
    workbench = json.loads((OUT / "workbench_manifest.json").read_text(encoding="utf-8"))
    groups = {model: bpy.data.collections[f"FP_{model}"] for model in WB.MODELS}
    actions = {}
    checks = []
    records = []
    for entry in source["animations"]:
        action, check = import_verified_action(entry)
        actions[(entry["model"], entry["variant"])] = action
        checks.append(check)
    for entry in source["animations"]:
        model = entry["model"]
        if model == "Pistol":
            continue  # 无枪托，前臂可达与镜头安全域由UE另一份报告检查。
        WB.show_group(groups, model)
        action = actions[(model, entry["variant"])]
        frame_count = int(round(action.frame_range[1] - action.frame_range[0]))
        phases = (0,) if entry["variant"] == "Idle" else sorted(set([index / frame_count for index in range(frame_count + 1)] + [sample["phase"] for sample in entry["samples"]]))
        for phase in phases:
            records.append(SKIN.evaluate_pose(model, entry["variant"], phase, "FinalUE"))
    # 不把候选通过当最终UE通过：此处报告仅包含FinalUE真实FBX动画，没有再解算候选IK。
    failures = [record for record in records if record["penetrating_vertices"] > 0]
    report = {"source": "FinalUE/current_ue_workbench_manifest.json", "units": "centimetres", "method": "final UE FBX actions on verified-identical original UE skinned mesh; actual evaluated stock triangle BVH; nearest-distance plus ray-parity", "contact_noise_cm": .02, "scope": "all exported animation frames plus all UE sample phases; all skin regions, not continuous triangle CCD", "reference_checks": checks, "total_samples": len(records), "failed_samples": len(failures), "max_depth_cm": max((record["max_depth_cm"] for record in records), default=0), "samples": records}
    (OUT / "workbench_final_ue_skin_diagnostic.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    rigs = {model: (bpy.data.objects[f"Arms_{model}"], bpy.data.objects[f"Gun_{model}"]) for model in WB.MODELS}
    gun_actions = {(model, variant): bpy.data.actions[f"A_Breach_{model}_Reload_{variant}"] for model in WB.MODELS for variant in ("Tactical", "Empty")}
    for model in WB.MODELS:
        mapping = Matrix(next(item["ue_to_blender"] for item in workbench["models"] if item["model"] == model))
        for stage, variant, phase in (("FinalGrip", "Idle", 0), ("FinalMagazine", "Tactical", .32), ("FinalMechanism", "Empty", .74)):
            WB.render_layout(model, stage, variant, phase, rigs, actions, gun_actions, groups, mapping, None, OUT / "WorkbenchPreviews")
    WB.show_group(groups, "Rifle")
    WB.assign_action(rigs["Rifle"][0], actions[("Rifle", "Idle")])
    WB.assign_action(rigs["Rifle"][1], None)
    for bone in rigs["Rifle"][1].pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    WB.set_frame(WB.action_frame(actions[("Rifle", "Idle")], 0))
    bpy.context.scene["final_ue_diagnostic"] = f"{len(records)} actual skin samples; {len(failures)} penetrating poses; see workbench_final_ue_skin_diagnostic.json"
    finalize_view_and_gallery()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "Breach_FP_Reload_Workbench.blend"))
    workbench["final_ue_actions"] = checks
    workbench["final_ue_skin_report"] = "workbench_final_ue_skin_diagnostic.json"
    workbench["status"] = "actual final UE animations imported; see final skin report and UE runtime validation"
    (OUT / "workbench_manifest.json").write_text(json.dumps(workbench, indent=2), encoding="utf-8")
    print(f"FP_FINAL_UE_SKIN_SUCCESS samples={len(records)} failures={len(failures)}", flush=True)


if __name__ == "__main__":
    if "--view-only" in sys.argv:
        # 只修打开视图与索引，复用已完成的FinalUE报告和动画；不重复耗时的导入/诊断。
        print("[FPWorkbench] call view-only entry", flush=True)
        bpy.ops.wm.open_mainfile(filepath=str(OUT / "Breach_FP_Reload_Workbench.blend"))
        finalize_view_and_gallery()
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "Breach_FP_Reload_Workbench.blend"))
    else:
        main()
