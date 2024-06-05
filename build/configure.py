import os

SRC = "src"
CONFIG = "config.h"
CORE = "core"
INDEX = "__index__"

CONFIGURATION = """
  #ifndef nat_config_h
  #define nat_config_h
  #define NAT_BASE_DIR "{base_dir}"
  #define NAT_CORE_LOC "{core_loc}" 
  #endif
"""

if __name__ == "__main__":
  base_dir = os.environ["NAT_BASE_DIR"]
  src_dir = os.path.join(base_dir, SRC)
  config = os.path.join(base_dir, SRC, CONFIG)

  with open(config, "w+") as f:
    configuration = CONFIGURATION.format(
      base_dir=base_dir,
      core_loc=os.path.join(src_dir, CORE, INDEX)
    )

    f.write(configuration)
