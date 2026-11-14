import json, os, urllib.request, sys

REPOS = {
    "galactus": ("SoldierDoge/GalactusV2", "217324c2fc516048effe0407595fa61c2ebc6863"),
    "chronos": ("KingJake3113/Chronos", "4f8fdd27e4802d977b33b9ade4759ff6aeaca5aa"),
    "dice": ("V1XT1/Dice-BotPackV5", "a3b9bd6b57c50ef0a50493a0e390eae6142bfa40"),
}
root = os.path.expanduser("~/opponent_ref_src")
for name, (repo, sha) in REPOS.items():
    tree = json.load(urllib.request.urlopen(f"https://api.github.com/repos/{repo}/git/trees/{sha}?recursive=1"))
    n = 0
    for t in tree["tree"]:
        p = t["path"]
        if t["type"] != "blob" or not p.startswith(("inc/RLGymCPP/", "inc/GigaLearnCPP/")):
            continue
        dst = os.path.join(root, name, p)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with urllib.request.urlopen(f"https://raw.githubusercontent.com/{repo}/{sha}/{p}") as r:
            open(dst, "wb").write(r.read())
        n += 1
    print(name, n, "files")
open(os.path.join(root, "SOURCES.txt"), "w").write("".join(f"{k}: https://github.com/{r} @ {s}\n" for k, (r, s) in REPOS.items()) + "Only inc/RLGymCPP/** (observation builders, game state, action parser) and inc/GigaLearnCPP/** (model code). Reference for tools/opponent_golden; not part of this repo.\n")
