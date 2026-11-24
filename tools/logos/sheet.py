import sys, glob, os
from PIL import Image

d = sys.argv[1]
files = sorted(glob.glob(os.path.join(d, "g*.png")))
tile = 400
cols = 5
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (cols * tile, rows * tile), "white")
for i, f in enumerate(files):
    im = Image.open(f).convert("RGB").resize((tile, tile), Image.LANCZOS)
    sheet.paste(im, ((i % cols) * tile, (i // cols) * tile))
out = os.path.join(d, "hoja_cristal.png")
sheet.save(out)
print(out)
