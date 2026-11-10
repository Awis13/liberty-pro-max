"""Identical diagnostic-only scripted-camera overlay for the pinned benchmark."""


def fixed_camera(source):
    anchor = "  CLEAR_CHAR_TASKS_IMMEDIATELY = 0x3C116620,\n"
    assert source.count(anchor) == 1
    source = source.replace(anchor, anchor + """  BEGIN_CAM_COMMANDS = 0x351F4C86,
  CREATE_CAM = 0x694A0DC1,
  SET_CAM_POS = 0x152F6314,
  POINT_CAM_AT_COORD = 0x4496175C,
  SET_CAM_FOV = 0x55D470C2,
  SET_CAM_ACTIVE = 0x43E42686,
  SET_CAM_PROPAGATE = 0x44414E60,
  ACTIVATE_SCRIPTED_CAMS = 0x3EBE11B9,
  GET_CAM_POS = 0x60C22E93,
  GET_CAM_FOV = 0x7BF4652D,
  IS_CAM_ACTIVE = 0x348D7AF5,
""")
    anchor = "}  // namespace\n\n// The main loop's wait for the render thread"
    assert source.count(anchor) == 1
    source = source.replace(anchor, """// Diagnostic camera: fixed elevated viewpoint, independent of ped/vehicle
// collision response. Natives/signatures: iv-sdk Scripting/{NativeHashes,BaseScripting}.h.
bool FixedCamera(Ctx& c, uint32_t base, const Scene& s, bool report) {
  static uint32_t camera = 0;
  if (!camera) {
    Wr32(base + kOut + 16, 0);
    Native(c, base, BEGIN_CAM_COMMANDS, {base + kOut + 16});
    Wr32(base + kOut, 0);
    Native(c, base, CREATE_CAM, {14, base + kOut});
    camera = Rd32(base + kOut);
  }
  if (!camera) { WarnF("gtaiv: bench: fixed camera creation failed\\n"); return false; }
  const float x = s.x + 7.0f, y = s.y, z = s.z + 5.5f;
  Native(c, base, SET_CAM_POS, {camera, Bits(x), Bits(y), Bits(z)});
  Native(c, base, POINT_CAM_AT_COORD, {camera, Bits(s.x - 1.0f), Bits(s.y), Bits(s.z + 0.8f)});
  Native(c, base, SET_CAM_FOV, {camera, Bits(55.0f)});
  Native(c, base, SET_CAM_ACTIVE, {camera, 1});
  Native(c, base, SET_CAM_PROPAGATE, {camera, 1});
  Native(c, base, ACTIVATE_SCRIPTED_CAMS, {1, 1});
  if (report) {
    Native(c, base, GET_CAM_POS, {camera, base + kOut, base + kOut + 4, base + kOut + 8});
    Native(c, base, GET_CAM_FOV, {camera, base + kOut + 12});
    const float cx = Float(Rd32(base + kOut)), cy = Float(Rd32(base + kOut + 4));
    const float cz = Float(Rd32(base + kOut + 8)), fov = Float(Rd32(base + kOut + 12));
    const bool active = Native(c, base, IS_CAM_ACTIVE, {camera}) != 0;
    WarnF("gtaiv: bench: fixed camera %.3f,%.3f,%.3f fov=%.3f active=%u\\n", cx, cy, cz, fov, active);
    return active && cx > x - 0.01f && cx < x + 0.01f && cy > y - 0.01f && cy < y + 0.01f &&
           cz > z - 0.01f && cz < z + 0.01f && fov > 54.99f && fov < 55.01f;
  }
  return true;
}

""" + anchor)
    anchor = "      if (!gameplay || Native(c, base, IS_CHAR_ON_FOOT, {ped})) {\n"
    assert source.count(anchor) == 1
    source = source.replace(anchor, """      const bool camera_ready = !gameplay || !std::getenv("LIBERTY_MEASURE_FIXED_CAMERA") || FixedCamera(c, base, s, true);
      if (camera_ready && (!gameplay || Native(c, base, IS_CHAR_ON_FOOT, {ped}))) {
""")
    anchor = "    }\n  }\n  c.eax = eax;\n"
    assert source.count(anchor) == 1
    source = source.replace(anchor, """    }
    if (placed && gameplay && std::getenv("LIBERTY_MEASURE_FIXED_CAMERA"))
      FixedCamera(c, base, s, frames % 240 == 0);
  }
  c.eax = eax;
""")
    return source
