# Prism Relay asset sources

Original geometry in meshes/ is authored for DotEngine.
It is stored as ordinary native mesh resources; Python is not needed to open or run the game.
The former crystal OBJ and material are removed with the collection-prototype props.
The former drone and round-feedback PCM are removed.

Downloaded texture files are unmodified 1K JPEGs from Poly Haven (CC0):

- Metal Plate, Rob Tuytel: https://polyhaven.com/a/metal_plate
- Concrete Floor Worn 001, photography Dimitrios Savva, processing Rico Cilliers:
  https://polyhaven.com/a/concrete_floor_worn_001
- Studio Small 09 HDRI, Sergej Majboroda: https://polyhaven.com/a/studio_small_09
- License: https://polyhaven.com/license

ARM files pack ambient occlusion in R, roughness in G and metallic in B.
Normal files use OpenGL +Y tangent-space convention. Color is sRGB; ARM and normals
are linear. Texture data is used in the actual PBR pipeline, not as baked lighting.
All inputs remain local and exports run offline.

## SHA-256 of upstream inputs

- `concrete_floor_worn_001_arm_1k.jpg`: `b21e63637d04b969894bd40a45df185d77671ed74b8955cd6e403c34ed465043`
- `concrete_floor_worn_001_diff_1k.jpg`: `6e40c0fc908f4d66431836f5abe203f3a4eb064e88824378a91a46a26e2f464c`
- `concrete_floor_worn_001_nor_gl_1k.jpg`: `523bd94a1be5dd2f11c2ae2b7d7c77445cf5bd9e41604db515a7c8364dcba067`
- `metal_plate_arm_1k.jpg`: `fc4708b6700a2cc387654b693091dd0ac0cd1797ae803d75c894e62554fbef07`
- `metal_plate_diff_1k.jpg`: `c6b6739eac2c75ccc898e753d7833df24eafbdb9aa92fb6eabfb92abd1a680a5`
- `metal_plate_nor_gl_1k.jpg`: `994f851a84d52b51e97906021a1b7070ee6eac9a789fef041132268de47d8e6b`
- `studio_small_09_1k.hdr`: `e7cfda5f4e98e623db12b8bfd0184e048488e4855d9c83e2751fb44a32e80c45`

## Surveyor mannequin and locomotion

- Quaternius, Universal Animation Library Standard:
  https://quaternius.com/packs/universalanimationlibrary.html
- Author's download page: https://quaternius.itch.io/universal-animation-library
- Pack license: CC0 1.0 Universal, retained at models/LICENSE.txt.
- Source file: Unreal-Godot/UAL1_Standard.glb, without root motion.
- Original archive SHA-256:
  cc73fc4e495b82958207316596317a3f40b9fa38065bde1027937452da537724
- Adapted models/surveyor.glb SHA-256:
  b0adf827bb729b2774ed1104f407720eeff9ba1aa684cdad081e8258f9a72915

The adaptation retains the real 65-joint mannequin, vertex weights and authored
idle/walk/jog/sprint/jump-start/airborne/landing tracks. Unrelated animation data is
removed, the editing pose uses the idle start, and the two materials use steel and
dark joint PBR factors. Native resources in surveyor/ are imported through the
editor's normal model operation. The scene parents the entire model/joint hierarchy
to the character; physics owns root motion. No procedural skeleton/animation stand-in
or runtime asset-generation script is required.

## Industrial equipment and ventilation

Utility Box 02 and Modular Airduct Rectangular 01 are Poly Haven CC0 assets:

- https://polyhaven.com/a/utility_box_02
- https://polyhaven.com/a/modular_airduct_rectangular_01
- https://polyhaven.com/license

Original 1K glTF/bin/JPEG inputs remain in models/, alongside PolyHaven-LICENSE.txt.
PolyHavenSources.json records all thirteen download URLs, byte sizes and SHA-256
hashes. Downloads were checked against the publisher's byte sizes and MD5 values.
The editor's ordinary model importer produced utility-box/ and airducts/; source
pixels, including signed normal RGB, are preserved in lossless native PNG images.
The imported materials are authored to use the ARM image's red channel as baked
occlusion; the upstream glTF binds its green/blue channels but omits that AO slot.
Straight/support scene assets were extracted and edited through product operations,
then instantiated/grouped in the main scene. Cabinets use fitted box colliders;
duct runs use simple box proxies. No mesh-generation helper or external authoring script is
required to open, edit or export the project.
