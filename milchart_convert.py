
"""
Convert Milthm in-game chart JSON to DevJson format for easy-phi.
Handles notes, lines, BPMs, and per-line animations/events.
"""

import json, sys, os

def convert(in_path, out_path=None):
    with open(in_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    meta = data.get('meta', {})
    bpms_in = data.get('bpms', [])
    lines_in = data.get('lines', [])


    bpm_events = []
    for b in bpms_in:
        t = b.get('time', [0, 0, 1])
        beat = t[0] + t[1] / t[2] if len(t) >= 3 else t[0]
        bpm_events.append({'beat': beat, 'bpm': b['bpm']})

    def beat_to_sec(target_beat):
        """Convert absolute beat number to seconds using BPM timeline."""
        if target_beat <= 0:
            return 0.0
        total = 0.0
        prev_beat = 0.0
        prev_bpm = bpm_events[0]['bpm'] if bpm_events else 120.0
        for be in bpm_events:
            if be['beat'] >= target_beat:
                total += (target_beat - prev_beat) * (60.0 / prev_bpm)
                return total
            total += (be['beat'] - prev_beat) * (60.0 / prev_bpm)
            prev_beat = be['beat']
            prev_bpm = be['bpm']
        total += (target_beat - prev_beat) * (60.0 / prev_bpm)
        return total

    def add_bpmidx(beat_arr, bpm_id=0):
        """Ensure beat array has 4 elements (append bpmIdx if needed)."""
        if len(beat_arr) >= 4:
            return list(beat_arr[:4])
        return list(beat_arr) + [bpm_id]

    def beat_arr_equal(a, b):
        """Check if two beat arrays represent the same time (for dedup)."""
        if len(a) < 3 or len(b) < 3:
            return False
        beat_a = a[0] + a[1] / a[2]
        beat_b = b[0] + b[1] / b[2]
        return abs(beat_a - beat_b) < 0.0001


    devjson = {
        "offset": meta.get("offset", 0),
        "meta": {
            "Title": meta.get("name", "Unknown"),
            "Composer": meta.get("music_artist", ""),
            "Illustrator": meta.get("background_artist") or meta.get("music_artist", ""),
            "Beatmapper": meta.get("charter", ""),
            "Difficulty": meta.get("difficulty_name", "Cloudburst"),
            "DifficultyValue": meta.get("difficulty", 0)
        },
        "bpms": [],
        "lines": [],
        "storyboardObjects": [],
        "animations": []
    }


    for be in bpm_events:
        devjson["bpms"].append({
            "start": beat_to_sec(be['beat']),
            "bpm": be['bpm']
        })


    for sb in data.get('storyboards', data.get('storyboardObjects', [])):
        devjson["storyboardObjects"].append(sb)

    event_global_index = 0


    for line_idx, line in enumerate(lines_in):

        dev_line = {"notes": []}
        for ni, note in enumerate(line.get('notes', [])):
            dev_note = {
                "bpm": 0,
                "startTime": note.get('time', [0, 0, 1]),
                "endTime": note.get('endTime', note.get('time', [0, 0, 1])),
                "type": note.get("type", 0),
                "isFake": note.get("isFake", False),
                "isAlwaysPerfect": note.get("isAlwaysPerfect", False),
                "index": ni
            }
            dev_line["notes"].append(dev_note)
        devjson["lines"].append(dev_line)


        for anim in line.get('animations', []):
            st = anim.get("startTime", [0, 0, 1])
            et = anim.get("endTime", [0, 0, 1])

            if beat_arr_equal(st, et) and anim.get("start") == anim.get("end") == 0:

                pass

            dev_anim = {
                "bpmId": 0,
                "fromBeat": add_bpmidx(st, 0),
                "toBeat": add_bpmidx(et, 0),
                "key": anim.get("type", 0),
                "fv": anim.get("start", 0),
                "tv": anim.get("end", 0),
                "data": anim.get("bearer_type", 0),
                "i1": anim.get("bearer", line_idx),
                "ease": anim.get("ease", {}).get("type", 0),
                "press": anim.get("ease", {}).get("press", 0)
            }
            devjson["animations"].append(dev_anim)
            event_global_index += 1

    out_name = out_path or os.path.splitext(in_path)[0] + '_dev.json'
    with open(out_name, 'w', encoding='utf-8') as f:
        json.dump(devjson, f, ensure_ascii=False, indent=2)

    note_cnt = sum(len(l["notes"]) for l in devjson["lines"])
    anim_cnt = len(devjson["animations"])
    print(f'[OK] DevJson written to: {out_name}')
    print(f'     {len(devjson["lines"])} lines, {note_cnt} notes, '
          f'{len(devjson["bpms"])} BPMs, {anim_cnt} animations')
    return devjson


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: python milchart_convert.py <chart.json> [output.json]')
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
