"""只读打开联合工作台，检查真实蒙皮顶点穿入真实枪托闭合表面；输出诊断，不改Content。"""
import importlib.util
import json
from pathlib import Path
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

# 所有路径只落诊断和美术预览目录；原UE FBX、模板及工作台Blend只读。
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Art/Player/Animations"
SPEC = importlib.util.spec_from_file_location("workbench", ROOT / "Tools/Blender/build_fp_reload_workbench.py")
WB = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(WB)


@WB.traced
def ray_inside(tree, point):
    """对疑似负距离顶点做奇偶射线复核，避免仅凭最近面法线误判凹形槽口。"""
    # 非轴向射线降低击中共边的概率；每次偏移0.001cm仅避免重复命中同一面。
    direction = Vector((.879, .351, .321)).normalized()
    origin = point.copy()
    count = 0
    for iteration in range(40):
        location, normal, index, distance = tree.ray_cast(origin, direction, 1000)
        if location is None:
            break
        count += 1
        origin = location + direction * .001
    return bool(count % 2)


@WB.traced
def evaluate_pose(model, variant, phase, prefix, frame=None):
    """设置同步动作并检查双臂真实顶点；frame可显式指定UE秒对应帧，排除FBX尾部重复帧的时轴缩放。"""
    arms = bpy.data.objects[f"Arms_{model}"]
    gun = bpy.data.objects[f"Gun_{model}"]
    action = bpy.data.actions[f"{prefix}_{model}_{variant}"]
    WB.assign_action(arms, action)
    WB.assign_action(gun, None if variant == "Idle" else bpy.data.actions[f"A_Breach_{model}_Reload_{variant}"])
    if variant == "Idle":
        for joint in gun.pose.bones:
            joint.matrix_basis = Matrix.Identity(4)
    WB.set_frame(WB.action_frame(action, phase) if frame is None else frame)
    graph = bpy.context.evaluated_depsgraph_get()
    mesh_object = bpy.data.objects[f"SK_UE_Arms_{model}"]
    evaluated = mesh_object.evaluated_get(graph)
    skin = evaluated.to_mesh()
    world_points = np.array([list(evaluated.matrix_world @ vertex.co) for vertex in skin.vertices])
    group_names = {group.index: group.name for group in mesh_object.vertex_groups}
    dominant = [group_names[max(vertex.groups, key=lambda item: item.weight).group] if vertex.groups else "unweighted" for vertex in mesh_object.data.vertices]
    record = {"model": model, "variant": variant, "phase": phase, "pose": prefix, "pieces": [], "penetrating_vertices": 0, "max_depth_cm": 0}
    for obj in bpy.data.collections[f"FP_{model}"].objects:
        if obj.type != "MESH" or not obj.name.startswith(WB.STOCK_PREFIXES):
            continue
        # 评估骨骼修改器后使用真实三角面，严禁用整枪包围盒判断实心区域。
        stock_object = obj.evaluated_get(graph)
        stock = stock_object.to_mesh()
        stock.calc_loop_triangles()
        vertices = [stock_object.matrix_world @ vertex.co for vertex in stock.vertices]
        triangles = [list(triangle.vertices) for triangle in stock.loop_triangles]
        tree = BVHTree.FromPolygons(vertices, triangles, all_triangles=True)
        bounds = np.array([list(point) for point in vertices])
        possible = np.flatnonzero(np.all((world_points >= bounds.min(axis=0)) & (world_points <= bounds.max(axis=0)), axis=1))
        hits = []
        for index in possible:
            point = Vector(world_points[index])
            nearest, normal, face_index, distance = tree.find_nearest(point)
            # 0.02cm仅排除数值接触噪声；每个真实穿入点用封闭表面奇偶射线二次确认。
            if nearest is not None and distance > .02 and (point - nearest).dot(normal) < 0 and ray_inside(tree, point):
                hits.append({"index": int(index), "dominant_bone": dominant[index], "depth_cm": float(distance), "weapon_cm": list(WB.GUN_BLENDER_TO_UE @ gun.matrix_world.inverted() @ point)})
        if hits:
            counts = {}
            for hit in hits:
                counts[hit["dominant_bone"]] = counts.get(hit["dominant_bone"], 0) + 1
            deepest = max(hits, key=lambda hit: hit["depth_cm"])
            record["pieces"].append({"piece": obj.name, "vertices": len(hits), "dominant_bones": counts, "deepest": deepest})
            record["penetrating_vertices"] += len(hits)
            record["max_depth_cm"] = max(record["max_depth_cm"], deepest["depth_cm"])
        stock_object.to_mesh_clear()
    evaluated.to_mesh_clear()
    print("FP_ACTUAL_SKIN", json.dumps(record), flush=True)
    return record


@WB.traced
def main():
    """当前快照/候选各检查关键阶段真实蒙皮，另外输出同相机旧姿态以便肉眼核对具体部位。"""
    bpy.ops.wm.open_mainfile(filepath=str(OUT / "Breach_FP_Reload_Workbench.blend"))
    manifest = json.loads((OUT / "workbench_manifest.json").read_text(encoding="utf-8"))
    groups = {model: bpy.data.collections[f"FP_{model}"] for model in WB.MODELS}
    records = []
    for model in ("Rifle", "Shotgun", "Sniper"):
        WB.show_group(groups, model)
        for prefix in ("CurrentUE", "Candidate"):
            for variant in ("Idle", "Tactical", "Empty"):
                # 非待机动作每个FBX实际60Hz帧都检查；额外机械关键时刻避免事件落在两帧之间漏测。
                action = bpy.data.actions[f"{prefix}_{model}_{variant}"]
                frame_count = int(round(action.frame_range[1] - action.frame_range[0]))
                phases = (0,) if variant == "Idle" else sorted(set([index / frame_count for index in range(frame_count + 1)] + [.16, .20, .28, .32, .50, .60, .68, .74, .88]))
                for phase in phases:
                    records.append(evaluate_pose(model, variant, phase, prefix))
    report = {"units": "centimetres", "method": "evaluated original UE skinned vertices inside evaluated real stock triangle BVH; nearest signed distance plus ray-parity confirmation", "contact_noise_cm": .02, "scope": "all 60Hz frames plus exact critical phases; all skin regions; not continuous mesh-triangle CCD", "samples": records}
    (OUT / "workbench_skin_diagnostic.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    # 旧图保持与已有候选图完全一致的几何相机；材质为工作台诊断替代色，并非UE最终材质。
    rigs = {model: (bpy.data.objects[f"Arms_{model}"], bpy.data.objects[f"Gun_{model}"]) for model in WB.MODELS}
    actions = {(model, variant): bpy.data.actions[f"CurrentUE_{model}_{variant}"] for model in WB.MODELS for variant in ("Idle", "Tactical", "Empty")}
    gun_actions = {(model, variant): bpy.data.actions[f"A_Breach_{model}_Reload_{variant}"] for model in WB.MODELS for variant in ("Tactical", "Empty")}
    for model, stage, variant, phase in (("Rifle", "CurrentGrip", "Idle", 0), ("Rifle", "CurrentMagazine", "Tactical", .32), ("Sniper", "CurrentMechanism", "Empty", .74)):
        mapping = Matrix(next(item["ue_to_blender"] for item in manifest["models"] if item["model"] == model))
        WB.render_layout(model, stage, variant, phase, rigs, actions, gun_actions, groups, mapping, None, OUT / "WorkbenchPreviews")
    print("FP_SKIN_DIAGNOSTIC_SUCCESS samples", len(records), flush=True)


if __name__ == "__main__":
    main()

