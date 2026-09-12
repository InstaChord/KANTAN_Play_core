Import("env")

import glob
import os


# The compiler cannot discover files referenced only by inline-assembly
# .incbin directives. Recompile sampler_app.cpp whenever an embedded factory
# sound changes so incremental builds never retain stale audio bytes.
project_dir = env.subst("$PROJECT_DIR")
asset_paths = glob.glob(
    os.path.join(project_dir, "docs", "Sample_Sound", "**", "*"),
    recursive=True,
)
assets = [env.File(path) for path in asset_paths if os.path.isfile(path)]
sampler_object = env.File(
    os.path.join(env.subst("$BUILD_DIR"), "src", "sampler", "sampler_app.cpp.o")
)
env.Depends(sampler_object, assets)
