# Skybox Glow Bug & Level-Finish Crash Investigation Notes

Status as of `2026-11-21`:
- **Investigation A (Skybox glow bug)**: Root cause confirmed, **`Block H2` (`skyboxProjWrap` at `0x4234d0..0x4235e0`, hooking `0x407805`) verified working in-game**, and `cam fov` label renamed to **`dolly zoom`** in [camera_angle.py](../patches/camera_angle.py), [find_free_slots.py](../tools/find_free_slots.py), and `edge.exe`. The earlier `0x52cfc0..0x52d0c0` slot was **abandoned** because `.data` holds 4 live SPARK particle callbacks (`0x5f7acc..0x5f7ad8`).
- **Investigation B (Level-finish crash)**: Separate issue documented intact in Section 2 below.

---

## 1. Investigation A: Missing Space Background Glow (`mobile` / `< 10` deg FOV)

### Observed Symptoms & Confirmed Root Cause
1. Original symptom: `mobile` or `< 10` `camera_fov` causes the level space background to lose its glow.
2. Why the earlier `0x52cfc0..0x52d0c0` `Block H2` build misbehaved (`fov = 5, dist = 1` still missing glow; further distances losing glow):
   - `.data` (`0x5f7acc..0x5f7ad8`) stores 4 live function pointers (`0x52d010`, `0x52d030`, `0x52d040`, `0x52d090`, SPARK DX9 callbacks dispatched from `0x533750` and `0x534254`) directly inside the old `0x52cfc0..0x52d0c0` slot, which `tools/find_free_slots.py --check-refs` previously missed because it did not scan `.data`.
   - `Block H2` also previously did not preserve `M_sky` on the stack cleanly or handle `Camera+0x30c` (`Frustum`).
3. **Confirmed asset & pipeline behavior (`2026-11-21`, verified against `skybox_1..4.rmdl` ESOs and `M_view` simulation)**:
   - `skybox_1..4.rmdl` (`[Renderer+0x58]`, `M_world = S(30) * T(cam_eye)`) renders `Sub[0]` (glow sphere, world `R = 1108.57`) and `Sub[1]` (starfield, world `R = 1018.92`), with root bounding sphere `R = 1920.09` (`[SkyRoot+0x1c0..0x1cc]`).
   - On the `Block H`-only build, Pause Menu `zoom = 5.0, dist = 2.0, fov = 25 deg` sets `near_H = 1046.7`, slicing `Sub[0]` (`arccos(1046.7 / 1108.57) = 19.2 deg` circular cap inside a `21.5 deg` half-width 16:9 view), explaining why the glow visibly shrank into a sphere and vanished as FOV decreased.
   - At smaller FOVs (`<= 15 deg`, `5 deg`, `0 deg`, `mobile`), `near` passes `1108.57` (100% GPU near-plane clip) and exceeds `1920.09` (`Frustum::testSphere` `0x4356a0` culls `SkyRoot` completely), while `< 22 deg` FOV zooms into a tiny portion of the `22 deg`-authored dome.

### Verified Engine & Live State (`PID 14108`, `level 309` `"1st contact"`, `1600x900`)
- **Active camera / globals**:
  - `G_CAM_FOV` (`0x5fa800`) = `5.0`, `G_CAM_DIST` (`0x5fa810`) = `1.0`, `G_ACTIVE_MUL` (`0x5fa820`) = `4.452041149139404`.
  - `ScreenMgr` (`0x5f9080 -> 0x8898cd8`): `+0x34` (`zoom`) = `1.25`, `+0x50` (`state`) = `0x11`, `+0x8c` = `22.0`, `+0xa0` = `3`.
  - `VP[0]` (`0x5f87d8..0x5f87dc` = `0x2ad1858..0x2ad1878`, `(0, 0, 1600, 900)`), `Camera` (`[vp+0x18] = 0x88b07d8`):
    - `+0x2b0` = `1` (perspective), `+0x2b4` = `0`
    - `+0x1c0..0x1dc` = `[0.0873, 1663.8293, 12891.6865, 0.0, 0.0, 1280.0, 720.0, 1.7778]` (`fovy = 5°`, `near = 1663.8293`, `far = 12891.6865`, `aspect = 1.7778`)
    - `+0x30c` (`Frustum`) = `[0.0437, 12891.6865, 1663.8293, 1.7778]` (`tan(fovy/2), far, near, aspect`)
    - `+0x304` = `0x88932a8` (`M_proj` diag `[12.88337, 22.90377, -1.148188, 0]`, `[2][3] = -1`, `[3][2] = -1910.388306`)
    - `+0x5c` (`M_view` bottom row) = `[-0.0, 244.799011, -3316.037598, 1.0]`
  - `Renderer` (`0x5f8770 -> 0x2ade820`): `+0x4 = 1`, `+0x58` (`skybox Model*`) = `0x15f6a628`, `+0xd0` (`clear color`) = `0xff000000`, `+0xd8` (`post-process active`) = `0`, `0x5f88e0` = `[0.0, 0.0, ...]`. Post-process bloom is disabled when `GS+0xa0 = 3`, so bloom is not required for the missing glow.
- **Distance & near/far math (`GameCamera::Update`, `0x47b8d0..0x47bc47`, `Block H` at `0x4248e0`)**:
  - Normalized offset vector `[0x5f9068..0x5f9070] = (1.0, 1.0, 1.0)` has magnitude `sqrt(3)`, so at `zoom = 1.25` and `dist = 300`, `d_eye = sqrt(3) * 300 * 1.25 * eff ≈ 649.52 * eff`.
  - `0x47bb80` and `0x47bba8` multiply both `near_max` (`900`) and `far0` (`8000`) by `viewScale = zoom * eff`, then `Block H` (`adjustNearFarHook` at `0x47bbc3`) shifts `[near, far]` to `[max(near_cur, 0.80 * d_eye - d_base), far_cur / eff + d_eye]` (`>= 10,000 + d_eye` when `mul > 1.0`).
- **3D render & skybox pipeline**:
  - `0x44e180` -> `Renderer::beginFrame` (`0x405f00`) -> per-viewport `Renderer::begin3D` (`0x4077c0`) -> `[ebx+0x10c] vtable +0x40` -> `0x4076d0`, `0x406690`, `0x407820`.
  - `Renderer::begin3D` (`0x4077c0`): `Viewport::begin` (`0x41fc70` -> `SetViewport`, `Camera::apply` `0x4235e0`, `BeginScene`) -> `0x420560` -> `0x407805` `renderSkybox` (`0x4075f0`).
  - `Camera::apply` (`0x4235e0`): calls `0x435780` (`SetTransform(3, [cam+0x304])`) and `0x4358e0` (`SetTransform(2, [cam+0x5c])`, resetting `MatrixStack` `0x5f87d0` top to identity).
  - `renderSkybox` (`0x4075f0`): disables `D3DRS_ZENABLE=0` (`7`) and `D3DRS_ZWRITEENABLE=0` (`0xe`), pushes `T(cam_pos)` onto `MatrixStack`, calls `Model::render(0)` (`0x421460`) on `[Renderer+0x58]`, pops matrix, calls `Renderer::flush` (`0x407410`), and restores `ZENABLE=1`/`ZWRITEENABLE=1`.
  - Deferred draw path: `ModelNode::render` (`0x4256d0`) defers nodes with `flags & 0x400` (`pass != 3`) via `0x407850` into `[Renderer+0x94]`. `Renderer::flush` (`0x407410`) draws them with `pass = 1` -> `0x425917` -> `ModelMeshNode` (`0x433e90`) -> `Camera::testSphere` (`0x423280` -> `Frustum::testSphere` `0x4356a0` on `[cam+0x30c]`) -> `SubMesh::draw` (`0x439e50`: `Material::apply` `0x437220`, `0x4064a0`, `MatrixStack::setWorld` `0x41f660`, `DrawIndexedPrimitive`).
  - Level loader (`0x47df00..0x47e2cb`): loads `"skybox_<theme+1>"`, sets model scale `30.0`, `flags |= 0x201`, `[+0xac] = 4`, assigns `[Renderer+0x58]`, and scales/places `LevelModel` at `[Game+0x213c]`.
- **Live `SkyModel` (`0x15f6a628`) & `LevelModel` (`0x15f6a400`) inspection (`PID 14108`)**:
  - `SkyRoot` (`0x15f6fd48`): `vis=1`, `alpha=1.0`, `flags=0x1500` (`0x400` deferred bit set), `sphere=[0, 0, 0, 64.0031]` (`r_world ≈ 1920.09`).
    - `SkySub[0]` (`0x15e950a8`): `mat=0x15f72ed8` (`flags=0x22`, `blend=(4,5)`, `emissive=0xff000000`, `diffuse=0xffffffff`, `tex[0]=0x15eae1a0`), `stride=24`, `IB=0x528`, `VB=0xdeeeb40`, CPU arrays `nV=2640, nC=3960, nUV=990, nTri=3960`, `local r = 0.786..0.900` (`x30 = 23.6..27.0`), `vert RGBA R=6..86, G=6..77, B=4..57, A=255`, `UV = 0.000..0.000` (from `[+0x8c..+0x90]`).
    - `SkySub[1]` (`0x15e95150`): `mat=0x15f73be8` (`flags=0x22`, `blend=(4,5)`, `emissive=0xff000000`, `diffuse=0xffffffff`, `tex[0]=0x15eae1a0`), `stride=20`, `IB=0x1192`, `VB=0xdeeec00`, `nV=8996, nC=0, nUV=3373, nTri=13494`, `local r = 0.082..1.702` (`x30 = 2.4..51.1`), `UV = 0.000..0.000` (from `[+0x8c..+0x90]`).
  - `LevelModel` (`0x15f6a400`): Root `0x15f6fb60` (`fl=0x1100`, `sp=[0,0,0,148.68]`), Child 1 `0x15f6f3c0` (`fl=0x100`, `sp=[-88.38, 583.12, 103.9, 2337.92]`, `mfl=0x0`), Child 2 `0x15f6f5a8` (`fl=0x500`, `sp=[18.42, 839.63, -367.59, 1064.61]`, `mfl=0x2`, `diff=0xff969696`, `em=0xffe5e5e5`).

### Current `Block H2` Implementation (`2026-11-21`, Verified In-Game)
1. **Verified safe slot**: `0x4234d0..0x4235e0` (`272` B, unused helper ending in 2 `int3` bytes before `Camera::apply` `0x4235e0`) has `0` external branches and `0` `.text`/`.rdata`/`.data` pointers (verified with upgraded [find_free_slots.py](../tools/find_free_slots.py)).
2. **`skyboxProjWrap` (`230 / 272` B, hook at `0x407805` -> `0x4075f0`)**:
   - Triggers when `[Renderer+0x58] != 0`, `checkInLevel != 0`, `cam != 0`, `[cam+0x2b0] == 1`, and (`cam->fovy < 22 deg` (`c_tbl+60 = 0x3ec49808`), `near > 500.0`, or `far < 2000.0`).
   - Allocates `0x50` bytes on stack (`M_sky` 64 B + saved `Camera+0x30c` `Frustum` 16 B), builds `D3DXMatrixPerspectiveFovRH(&M_sky, max(fovy, 22 deg), aspect, 100.0f, 8000.0f)` (`0x4bb2d0`), applies via `Camera::SetProj` (`0x435780`, `ecx = &M_sky`, `push 0` matching `ret 4`), sets `Camera+0x30c` `Frustum` to `tan = 1.0, far = 8000.0, near = 100.0`, calls `0x4075f0`, then restores `Camera+0x30c` `Frustum`, frees stack, and restores `D3DTS_PROJECTION` from `[cam+0x304]`.
3. **In-game check needed**: Test on `level 309` ("1st contact") across `(dist=1, fov=pc/5/0/mobile)` and `(dist=2, fov=25/20/15/10/5/mobile)` in both gameplay and Pause Menu.

---

## 2. Investigation B: Level-Finish Crash (`BEX / StackHash_2beb / 0xc0000005` at `0x00000010`)

### Observed Event Logs & WER Reports (`2026-11-19`)
All 6 logged crashes today share the identical signature:
- **Events**: `12:54:06` (`PID 0x222C`), `12:54:36` (`PID 0x6090`), `12:56:07` (`PID 0x4B60`), `12:58:14`, `13:00:27` (`PID 0x3414` = `13332`), `13:01:06` (`PID 0x5DD8`).
- **WER Bucket**: `1680375644350894805` (`039e7dd32b8d6d4a4751e4bedac0d2d5`), `EventType: BEX`, `Fault Module Name: StackHash_2beb`, `Exception Offset: PCH_F3_FROM_ntdll+0x0007890C`, `Exception Code: c0000005`, `Exception Data: 00000008` (DEP / execute fault), `Fault offset: 0x00000010`.
- **Critically**: The crashes at `13:00:27` (`PID 13332`, started `12:58:51` after applying the `EDGE_NO_H2=1` build sha256 `082f0c464fbf9f4dd7b632d51cb35ccce0e4d79306e61b4a97d8379e9b6b7164`) and `13:01:06` (`PID 0x5DD8`) occurred **with `0x407805` unhooked** (`call 0x4075f0` restored).

### Next Steps for Crash Bisect
- **Inference**: Since disabling the `0x407805` skybox hook did not stop the level-finish crash, the crash likely comes from another patch active during level finish / debrief (e.g., `ui_smooth_ext.py` modified `2026-11-19 10:50:44` or another module).
- Reproduce with `--no-ui-smooth` (or bisect modules in `patch_edge.py`) and/or attach a local minidump / debugger to capture the exact call stack before jumping to `0x00000010`.
