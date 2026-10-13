import sys
def fatal(*args): print(*args, file=sys.stderr); sys.exit(1)
def bold(text): return text
