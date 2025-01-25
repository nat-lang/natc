import os

SRC = "src/"

CONFIGURATION = """
  #ifndef nat_config_h
  #define nat_config_h
  #define NAT_SRC_DIR "{src_dir}"
  #define NAT_CORE_LOC "{core_loc}" 
  #define NAT_SYSTEM_LOC "{system_loc}"
  #endif
"""

if __name__ == "__main__":
  config = os.path.join(os.getcwd(), SRC, "config.h")

  with open(config, "w+") as f:
    configuration = CONFIGURATION.format(
      src_dir=SRC,
      core_loc=os.path.join("core", "index"),
      system_loc=os.path.join("core", "system")
    )

    f.write(configuration)
