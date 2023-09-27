
with open('test_arith.S', 'r') as f:
    out = ""
    lines = f.readlines()
    num = 1
    start = False
    for line in lines:
        if start:
            toks = line.split(",")
            if len(toks) == 1:
                out += line
                continue
            out += toks[0] + ", " + str(num) + "," + ",".join(toks[2:])
            num += 1
        else:
            out += line

        if line.startswith("_start:"):
            start = True

    print(out)
