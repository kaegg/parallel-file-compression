#!/usr/bin/env python3
import csv
import textwrap
from pathlib import Path


PAGE_WIDTH = 595
PAGE_HEIGHT = 842
MARGIN_LEFT = 54
MARGIN_TOP = 58
MARGIN_BOTTOM = 52


def pdf_escape(text):
    text = text.replace("\\", "\\\\")
    text = text.replace("(", "\\(")
    text = text.replace(")", "\\)")
    return text


class SimplePDF:
    def __init__(self):
        self.pages = []
        self.current = []
        self.y = PAGE_HEIGHT - MARGIN_TOP

    def add_page(self):
        if self.current:
            self.pages.append("\n".join(self.current))
        self.current = []
        self.y = PAGE_HEIGHT - MARGIN_TOP

    def ensure_space(self, needed):
        if self.y - needed < MARGIN_BOTTOM:
            self.add_page()

    def text(self, value, x=MARGIN_LEFT, size=10, font="F1", leading=13):
        self.ensure_space(leading)
        safe = pdf_escape(value)
        self.current.append(f"BT /{font} {size} Tf {x} {self.y:.2f} Td ({safe}) Tj ET")
        self.y -= leading

    def paragraph(self, value, size=10, font="F1", width=92, leading=13):
        for line in textwrap.wrap(value, width=width, replace_whitespace=False):
            self.text(line, size=size, font=font, leading=leading)
        self.y -= 3

    def heading(self, value, level):
        if level == 1:
            self.ensure_space(36)
            self.text(value, size=18, font="F2", leading=24)
        elif level == 2:
            self.ensure_space(28)
            self.text(value, size=14, font="F2", leading=20)
        else:
            self.ensure_space(22)
            self.text(value, size=12, font="F2", leading=17)

    def code_line(self, value):
        for line in textwrap.wrap(value, width=96, replace_whitespace=False):
            self.text(line, size=8, font="F3", leading=10)

    def rect(self, x, y, w, h, gray=0.9):
        self.current.append(f"{gray} g {x:.2f} {y:.2f} {w:.2f} {h:.2f} re f 0 g")

    def line(self, x1, y1, x2, y2):
        self.current.append(f"{x1:.2f} {y1:.2f} m {x2:.2f} {y2:.2f} l S")

    def chart(self, title, labels, values, unit="s"):
        self.add_page()
        self.heading(title, 2)
        chart_x = 95
        chart_y = 190
        chart_w = 390
        chart_h = 460
        max_value = max(values) if values else 1.0
        bar_gap = 18
        bar_w = (chart_w - (len(values) - 1) * bar_gap) / len(values)

        self.line(chart_x, chart_y, chart_x, chart_y + chart_h)
        self.line(chart_x, chart_y, chart_x + chart_w, chart_y)

        for index, (label, value) in enumerate(zip(labels, values)):
            bar_h = (value / max_value) * chart_h if max_value > 0 else 0
            x = chart_x + index * (bar_w + bar_gap)
            self.rect(x, chart_y, bar_w, bar_h, gray=0.78)
            self.current.append(f"{x:.2f} {chart_y:.2f} {bar_w:.2f} {bar_h:.2f} re S")
            self.current.append(
                f"BT /F3 8 Tf {x:.2f} {chart_y - 16:.2f} Td ({pdf_escape(label)}) Tj ET"
            )
            self.current.append(
                f"BT /F3 8 Tf {x:.2f} {chart_y + bar_h + 8:.2f} Td ({value:.3f} {unit}) Tj ET"
            )

        self.y = chart_y - 45
        self.paragraph(
            "Grafico de barras gerado a partir dos tempos medidos no ambiente Linux do projeto.",
            size=9,
            width=86,
            leading=12,
        )

    def finish(self):
        if self.current:
            self.pages.append("\n".join(self.current))

    def write(self, output_path):
        self.finish()
        objects = []

        catalog_id = 1
        pages_id = 2
        font_regular_id = 3
        font_bold_id = 4
        font_mono_id = 5
        page_ids = []
        content_ids = []
        next_id = 6

        for _ in self.pages:
            page_ids.append(next_id)
            content_ids.append(next_id + 1)
            next_id += 2

        objects.append((catalog_id, f"<< /Type /Catalog /Pages {pages_id} 0 R >>"))
        kids = " ".join(f"{page_id} 0 R" for page_id in page_ids)
        objects.append((pages_id, f"<< /Type /Pages /Kids [{kids}] /Count {len(page_ids)} >>"))
        objects.append((font_regular_id, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"))
        objects.append((font_bold_id, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>"))
        objects.append((font_mono_id, "<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>"))

        for page_id, content_id, stream in zip(page_ids, content_ids, self.pages):
            resources = (
                "<< /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R >> >>"
            )
            page_obj = (
                f"<< /Type /Page /Parent {pages_id} 0 R "
                f"/MediaBox [0 0 {PAGE_WIDTH} {PAGE_HEIGHT}] "
                f"/Resources {resources} /Contents {content_id} 0 R >>"
            )
            stream_bytes = stream.encode("latin-1", "replace")
            content_obj = (
                f"<< /Length {len(stream_bytes)} >>\n"
                f"stream\n{stream}\nendstream"
            )
            objects.append((page_id, page_obj))
            objects.append((content_id, content_obj))

        pdf = bytearray()
        pdf.extend(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
        offsets = [0]
        for object_id, content in sorted(objects, key=lambda item: item[0]):
            offsets.append(len(pdf))
            pdf.extend(f"{object_id} 0 obj\n".encode("ascii"))
            pdf.extend(content.encode("latin-1", "replace"))
            pdf.extend(b"\nendobj\n")

        xref_offset = len(pdf)
        pdf.extend(f"xref\n0 {len(offsets)}\n".encode("ascii"))
        pdf.extend(b"0000000000 65535 f \n")
        for offset in offsets[1:]:
            pdf.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
        pdf.extend(
            (
                f"trailer\n<< /Size {len(offsets)} /Root {catalog_id} 0 R >>\n"
                f"startxref\n{xref_offset}\n%%EOF\n"
            ).encode("ascii")
        )

        Path(output_path).write_bytes(pdf)


def add_markdown(pdf, markdown_path):
    in_code = False
    for raw_line in Path(markdown_path).read_text(encoding="utf-8").splitlines():
        line = raw_line.rstrip()
        if line.startswith("```"):
            in_code = not in_code
            pdf.y -= 4
            continue
        if in_code:
            pdf.code_line(line)
            continue
        if not line:
            pdf.y -= 6
            if pdf.y < MARGIN_BOTTOM:
                pdf.add_page()
            continue
        if line.startswith("# "):
            pdf.heading(line[2:].strip(), 1)
        elif line.startswith("## "):
            pdf.heading(line[3:].strip(), 2)
        elif line.startswith("### "):
            pdf.heading(line[4:].strip(), 3)
        elif line.startswith("|"):
            pdf.code_line(line)
        elif line.startswith("- "):
            pdf.paragraph("• " + line[2:], width=88)
        else:
            cleaned = line.replace("**", "").replace("`", "")
            pdf.paragraph(cleaned)


def load_data(csv_path):
    with Path(csv_path).open(newline="", encoding="utf-8") as csv_file:
        return list(csv.DictReader(csv_file))


def main():
    base = Path(__file__).parent
    markdown_path = base / "relatorio_tecnico.md"
    csv_path = base / "dados_desempenho.csv"
    output_path = base / "relatorio_tecnico.pdf"

    data = load_data(csv_path)
    pdf = SimplePDF()
    add_markdown(pdf, markdown_path)

    compression_rows = [
        row for row in data
        if row["operacao"] == "compressao"
    ]
    pdf.chart(
        "Grafico 1 - Tempo total de compressao",
        [row["configuracao"] if row["threads"] == "1" else f"{row['threads']} th" for row in compression_rows],
        [float(row["total_s"]) for row in compression_rows],
    )
    pdf.chart(
        "Grafico 2 - Tempo de processamento da compressao",
        [row["configuracao"] if row["threads"] == "1" else f"{row['threads']} th" for row in compression_rows],
        [float(row["processamento_s"]) for row in compression_rows],
    )

    decompression_rows = [
        row for row in data
        if row["operacao"] == "descompressao" and row["configuracao"] != "sequencial-paralelo"
    ]
    pdf.chart(
        "Grafico 3 - Tempo total de descompressao",
        ["seq" if row["threads"] == "1" else f"{row['threads']} th" for row in decompression_rows],
        [float(row["total_s"]) for row in decompression_rows],
    )
    pdf.chart(
        "Grafico 4 - Tempo de processamento da descompressao",
        ["seq" if row["threads"] == "1" else f"{row['threads']} th" for row in decompression_rows],
        [float(row["processamento_s"]) for row in decompression_rows],
    )

    pdf.write(output_path)


if __name__ == "__main__":
    main()
