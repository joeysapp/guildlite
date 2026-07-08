#!/usr/bin/env python3
# Extract GWToolbox's static armor table (ArmoryWindow_Constants.h) into a portable
# TSV: model_file_id -> name, profession, slot, campaign, default dye. This is the
# 'item' half of the armor item->texture mapping; the model_file_id -> DAT-file-ids
# hop is a runtime composite dump (GetCompositeModelInfoArray), joined later.
import re, sys

def main(src_path, out_path):
    src = open(src_path, encoding='utf-8', errors='replace').read()
    pat = re.compile(
        r'\{\s*"([^"]+)"\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*'
        r'Profession::(\w+)\s*,\s*ItemType::(\w+)\s*,\s*Campaign::(\w+)\s*,\s*(\d+)')
    rows = set()
    for m in pat.finditer(src):
        name, hexid, prof, slot, camp, dye = m.groups()
        mfid = int(hexid, 16)
        if mfid == 0:            # skip the "NoChest"/"NoHead" placeholders
            continue
        rows.add((mfid, hexid, name, prof, slot, camp, int(dye)))
    rows = sorted(rows)
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write("#model_file_id\thex\tname\tprofession\tslot\tcampaign\tdye_tint\n")
        for mfid, hexid, name, prof, slot, camp, dye in rows:
            f.write(f"{mfid}\t{hexid}\t{name}\t{prof}\t{slot}\t{camp}\t{dye}\n")
    print(f"extracted {len(rows)} armors -> {out_path}")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
