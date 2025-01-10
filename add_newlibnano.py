Import("env")

# nona.specs 将 -lc 替换成 -lc_nano ,使精简版的C库替代标准C库
# nosys.specs告诉链接器不链接任何系统库
env.Append(LINKFLAGS=["--specs=nosys.specs", "--specs=nano.specs"])
