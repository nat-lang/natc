import os

SRC = "src"

CONFIGURATION = """
  #ifndef nat_config_h
  #define nat_config_h
  #define NAT_CORE_LOC "{core_loc}" 
  #define NAT_SYSTEM_LOC "{system_loc}"
  #endif
"""

if __name__ == "__main__":
  src_dir = os.path.join(os.getcwd(), SRC)
  cfg_loc = os.path.join(src_dir, "config.h")
  core_dir = os.path.join(src_dir, "core")

  with open(cfg_loc, "w+") as f:
    configuration = CONFIGURATION.format(
      core_loc=os.path.join(core_dir, "index"),
      system_loc=os.path.join(core_dir, "system")
    )

    f.write(configuration)
