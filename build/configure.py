import os

SRC = "src"

CONFIGURATION = """
  #ifndef nat_config_h
  #define nat_config_h
  #ifdef __EMSCRIPTEN__
    #define NAT_CORE_LOC "{ems_core_loc}" 
    #define NAT_SYSTEM_LOC "{ems_system_loc}"
  #else
    #define NAT_CORE_LOC "{core_loc}" 
    #define NAT_SYSTEM_LOC "{system_loc}"
  #endif
  #endif
"""

if __name__ == "__main__":
  proj_dir = os.path.join(os.getcwd())
  cfg_loc = os.path.join(SRC, "config.h")
  core_dir = os.path.join(SRC, "core")
  ems_dir = "/"

  with open(cfg_loc, "w+") as f:
    configuration = CONFIGURATION.format(
      core_loc=os.path.join(proj_dir, core_dir, "index"),
      system_loc=os.path.join(proj_dir, core_dir, "system"),
      ems_core_loc=os.path.join(ems_dir, core_dir, "index"),
      ems_system_loc=os.path.join(ems_dir, core_dir, "system"),
    )

    f.write(configuration)
