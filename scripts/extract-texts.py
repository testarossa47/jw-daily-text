import zipfile, json, os, re, sys, argparse
from datetime import date, timedelta

DAYS_IN_MONTH = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]


def strip_html(text):
    return re.sub(r'\s+', ' ', re.sub(r'<[^>]+>', '', text)).replace('\u200b', '').strip()


def detect_prefix(z):
    names = z.namelist()
    prefix_pattern = re.compile(r'OEBPS/(1102\d+?)\d\d\.xhtml$')
    for n in names:
        m = prefix_pattern.search(n)
        if m:
            return m.group(1)
    return "11020262"


def extract(epub_path):
    texts = []
    with zipfile.ZipFile(epub_path, 'r') as z:
        prefix = detect_prefix(z)
        basename = os.path.basename(epub_path)
        ym = re.search(r'es(\d{2})', basename)
        year = 2000 + int(ym.group(1)) if ym else 2026

        start_date = date(year, 1, 1)
        is_leap = (year % 4 == 0 and (year % 100 != 0 or year % 400 == 0))
        dim = DAYS_IN_MONTH[:]
        if is_leap:
            dim[1] = 29

        day_of_year = 0
        for i in range(12):
            month_id = f"{prefix}{i:02d}"
            main_file = f"OEBPS/{month_id}.xhtml"
            split_files = []
            for d in range(2, 32):
                sf = f"OEBPS/{month_id}-split{d}.xhtml"
                try:
                    z.getinfo(sf)
                    split_files.append(sf)
                except KeyError:
                    break
            all_files = [main_file] + split_files

            for file_idx, fpath in enumerate(all_files):
                current_day_offset = day_of_year + file_idx
                max_days = 366 if is_leap else 365
                if current_day_offset >= max_days:
                    break
                try:
                    content = z.read(fpath).decode('utf-8')
                except KeyError:
                    continue

                current_date = start_date + timedelta(days=current_day_offset)
                date_str = current_date.strftime("%Y-%m-%d")

                themeScrp = re.search(
                    r'<p[^>]*class="themeScrp"[^>]*>(.*?)</p>', content, re.DOTALL
                )
                if not themeScrp:
                    continue

                theme_html = themeScrp.group(1)
                ref_match = re.search(r'<a[^>]*>(.*?)</a>', theme_html)
                ref = ""
                if ref_match:
                    candidate = strip_html(ref_match.group(1))
                    if re.search(r'\d', candidate):
                        ref = candidate

                em_matches = re.findall(r'<em>(.*?)</em>', theme_html)
                text_parts = []
                for em in em_matches:
                    em_text = strip_html(em)
                    if em_text and ref not in em_text:
                        text_parts.append(em_text)
                text = " ".join(text_parts).strip().rstrip(',').strip()

                bodyMatch = re.search(
                    r'<div class="bodyTxt">(.*?)</div>', content, re.DOTALL
                )
                commentary = ""
                if bodyMatch:
                    p_match = re.search(
                        r'<p[^>]*class="[^"]*p\d+[^"]*"[^>]*>(.*?)</p>',
                        bodyMatch.group(1), re.DOTALL
                    )
                    if p_match:
                        commentary = strip_html(p_match.group(1))
                        commentary = commentary.rsplit('\u2014', 1)[0].strip()
                        commentary = re.sub(r'\s*\.\s*$', '', commentary)

                texts.append({
                    "date": date_str,
                    "ref": ref,
                    "text": text,
                    "commentary": commentary
                })

            day_of_year += dim[i]

    return texts, year


def generate_js_module(json_path, js_path, month=None):
    """Generate jw-texts.js from jw-texts.json.

    If month is None, includes all data.
    If month is given (1-12), only that month's entries are included.
    """
    with open(json_path, 'r', encoding='utf-8') as f:
        entries = json.load(f)

    years = {}
    for entry in entries:
        if month is not None:
            entry_month = int(entry["date"].split("-")[1])
            if entry_month != month:
                continue
        y = entry["date"][:4]
        if y not in years:
            years[y] = {}
        years[y][entry["date"]] = {
            "ref": entry["ref"],
            "text": entry["text"],
            "commentary": entry["commentary"]
        }

    js_lines = ["export const texts = {"]
    year_keys = sorted(years.keys())
    for yi, y in enumerate(year_keys):
        js_lines.append(f'\t"{y}": {{')
        date_keys = sorted(years[y].keys())
        for di, d in enumerate(date_keys):
            e = years[y][d]
            js_lines.append(f'\t\t"{d}": {{')
            js_lines.append(f'\t\t\tref: {json.dumps(e["ref"])},')
            js_lines.append(f'\t\t\ttext: {json.dumps(e["text"])},')
            js_lines.append(f'\t\t\tcommentary: {json.dumps(e["commentary"])}')
            comma = "," if di < len(date_keys) - 1 else ""
            js_lines.append(f"\t\t}}{comma}")
        comma = "," if yi < len(year_keys) - 1 else ""
        js_lines.append(f"\t}}{comma}")
    js_lines.append("};")

    with open(js_path, 'w', encoding='utf-8') as f:
        f.write("\n".join(js_lines) + "\n")

    count = sum(len(dates) for dates in years.values())
    label = f"month {month}" if month else "all months"
    print(f"Generated {js_path} ({count} entries, {label})")


def generate_phone_data(json_path, js_path):
    """Generate pkjs/full-data.js with the complete dataset embedded."""
    with open(json_path, 'r', encoding='utf-8') as f:
        entries = json.load(f)

    years = {}
    for entry in entries:
        y = entry["date"][:4]
        m = entry["date"][5:7]
        if y not in years:
            years[y] = {}
        if m not in years[y]:
            years[y][m] = {}
        years[y][m][entry["date"]] = {
            "ref": entry["ref"],
            "text": entry["text"],
            "commentary": entry["commentary"]
        }

    json_str = json.dumps(years, ensure_ascii=False, separators=(",", ":"))
    content = "const FULL_DATA = " + json_str + ";\n"
    with open(js_path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"Generated {js_path} ({len(entries)} entries)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Extract daily texts from JW EPUB")
    parser.add_argument("epub", nargs="?", default="/tmp/es26_E.epub",
                        help="Path to EPUB file")
    parser.add_argument("--month", type=int, choices=range(1, 13), default=None,
                        help="Only include this month in the bundled JS")
    args = parser.parse_args()

    base_dir = os.path.dirname(os.path.dirname(__file__))
    output_dir = os.path.join(base_dir, "resources")
    output_path = os.path.join(output_dir, "jw-texts.json")
    os.makedirs(output_dir, exist_ok=True)

    texts, year = extract(args.epub)
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(texts, f, indent=2, ensure_ascii=False)
    print(f"Extracted {len(texts)} daily texts ({year}) to {output_path}")

    js_path = os.path.join(base_dir, "src", "embeddedjs", "jw-texts.js")
    generate_js_module(output_path, js_path, month=args.month)

    phone_data_path = os.path.join(base_dir, "src", "pkjs", "full-data.js")
    generate_phone_data(output_path, phone_data_path)

    if texts:
        print(json.dumps(texts[0], indent=2, ensure_ascii=False))
