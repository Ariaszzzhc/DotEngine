# Prism Relay asset sources

Original geometry in meshes/ is authored for DotEngine.
It is stored as ordinary native mesh resources; Python is not needed to open or run the game.
The original crystal OBJ and feedback PCM remain repository-authored assets.

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
