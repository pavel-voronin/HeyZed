import unreal

if not unreal.HeyZedMotionAuthoring.prepare_graph():
    raise RuntimeError("Cannot prepare the compiled locomotion AnimGraph")
unreal.log("heyzed compiled locomotion AnimGraph ready")
