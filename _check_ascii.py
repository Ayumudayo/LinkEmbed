from pathlib import Path
non_ascii = {}
for path in Path('.').rglob('*'):
    if path.is_file() and path.suffix.lower() in {'.cpp','.hpp','.md','.json'}:
        try:
            text = path.read_text(encoding='utf-8')
        except Exception:
            continue
        if any(ord(ch) > 127 for ch in text):
            lines = []
            for idx,line in enumerate(text.splitlines(),1):
                if any(ord(ch) > 127 for ch in line):
                    lines.append((idx,line))
            non_ascii[path] = lines
if non_ascii:
    for path, lines in non_ascii.items():
        print(path)
        for idx,line in lines:
            print(f"  {idx}: {line}")
else:
    print('No non-ASCII characters detected.')
