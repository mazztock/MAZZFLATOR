#!/usr/bin/env python3
"""MAZZFLATOR : retouche le skin après sa fabrication (appelé par le SDK : python3 skin_post.py <dossier du skin>).
 - faders : on enlève le nom et la valeur affichés par la MPC
 - boutons : on enlève le nom de la MPC et on ÉCRIT le texte du bouton (label du layout) dans ses images on/off.
   Le contrôle reste un toggle intact, donc le toucher fonctionne comme avant.
Réglages : voir juste en dessous."""
import copy
import glob
import json
import os
import re
import shutil
import sys

TEXT_COLOUR = None                # None = automatique : noir, sauf sur une image sombre où le texte passe en blanc.
                                  # Pour forcer : (0, 0, 0, 255) = noir, (255, 255, 255, 255) = blanc
DARK_BELOW = 100                  # luminosité (0-255) du centre du bouton en dessous de laquelle le texte devient blanc
TEXT_SIZE = 17                    # hauteur du texte en pixels (réduite automatiquement si le mot est trop large)
MAX_WIDTH = 52                    # largeur maximale du texte sur un bouton de 70 px


WHITE_LIT = ()            # boutons dont l'image allumée (la plus colorée des deux) est convertie en blanc
KEEP_VALUE = ("GAIN",)    # affichages de valeur (texte en dB) : le nom de la MPC est retiré, la valeur est gardée et restylée
VALUE_SIZE = 24.0         # hauteur du texte de la valeur
VALUE_COLOUR = "ffffe23a" # couleur du texte de la valeur (jaune, skin 2026)
NO_TEXT = ("AA", "AG", "CLIP", "SPLIT", "BAND", "FX ON")  # skin 2026 : le texte est déjà dessiné dans les images         # boutons (LED) qui ne reçoivent aucun texte : le nom de la MPC est retiré, rien n'est écrit dedans


def mean_sat(png):
    from PIL import Image, ImageStat
    return ImageStat.Stat(Image.open(png).convert("RGB").convert("HSV").getchannel("S")).mean[0]


def make_white(png):
    """Image allumée en blanc lumineux : on retire la couleur et on éclaircit les tons moyens, le contour sombre reste."""
    from PIL import Image
    im = Image.open(png).convert("RGBA")
    alpha = im.getchannel("A")
    h, s, v = im.convert("RGB").convert("HSV").split()
    s = s.point(lambda x: int(x * 0.04))
    v = v.point(lambda x: min(255, int(((x / 255.0) ** 0.75) * 1.10 * 255)))
    out = Image.merge("HSV", (h, s, v)).convert("RGB").convert("RGBA")
    out.putalpha(alpha)
    out.save(png)


def is_label(k, which):
    cd = k["componentData"]
    return cd["type"] == "Label" and cd["data"].get("type") in which


def find_font(size):
    from PIL import ImageFont
    here = os.path.dirname(os.path.abspath(__file__))
    pats = [here + "/fonts/*.ttf", here + "/fonts/*.otf",
            "/mv/tools/**/*itillium*emi*old*.ttf", "/mv/tools/**/*itillium*old*.ttf", "/mv/tools/**/*itillium*.ttf",
            "/usr/share/fonts/**/DejaVuSans-Bold.ttf", "/usr/share/fonts/**/*.ttf"]
    for p in pats:
        for f in sorted(glob.glob(p, recursive=True)):
            try:
                return ImageFont.truetype(f, size), f
            except Exception:
                pass
    try:
        return ImageFont.load_default(size=size), "défaut Pillow"
    except TypeError:
        return ImageFont.load_default(), "défaut Pillow (petit)"


def draw_label(png, text):
    from PIL import Image, ImageDraw
    im = Image.open(png).convert("RGBA")
    size = TEXT_SIZE
    while True:
        font, src = find_font(size)
        d = ImageDraw.Draw(im)
        box = d.textbbox((0, 0), text, font=font)
        w, h = box[2] - box[0], box[3] - box[1]
        if w <= MAX_WIDTH or size <= 8:
            break
        size -= 1
    x = (im.width - w) / 2 - box[0]
    y = (im.height - h) / 2 - box[1]
    fill = TEXT_COLOUR
    if fill is None:
        c = im.crop((im.width // 2 - 20, im.height // 2 - 12, im.width // 2 + 20, im.height // 2 + 12)).convert("L")
        px = c.tobytes()
        fill = (0, 0, 0, 255) if sum(px) / len(px) >= DARK_BELOW else (255, 255, 255, 255)
    d.text((x, y), text, font=font, fill=fill)
    im.save(png)
    return src, size


def split_shared_toggles(doc, folder):
    """Deux boutons qui partagent la même image partagent aussi une définition : on la duplique (avec une copie des
    images) pour que chacun puisse porter son propre texte."""
    dlist = doc["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    defs = {d["key"]: d for d in dlist}
    users = {}
    for d in dlist:
        for k in d["value"]["componentsData"]:
            t = k["componentData"]["type"]
            if t in defs and t.startswith("shToggle"):
                users.setdefault(t, []).append(k)
    for key, kids in users.items():
        names = []
        for k in kids:
            n = k["componentData"]["name"].strip()
            if n and n not in names:
                names.append(n)
        for n in names[1:]:                                   # le premier garde la définition d'origine
            slug = re.sub(r"[^A-Za-z0-9]+", "_", n).strip("_") or "x"
            nd = copy.deepcopy(defs[key])
            nd["key"] = key + "_" + slug
            btn = next(c for c in nd["value"]["componentsData"] if c["componentData"]["type"] == "Button")
            for fld in ("offImage", "onImage"):
                old = btn["componentData"]["data"][fld]
                new = os.path.splitext(old)[0] + "_" + slug + os.path.splitext(old)[1]
                shutil.copyfile(os.path.join(folder, old), os.path.join(folder, new))
                btn["componentData"]["data"][fld] = new
            dlist.append(nd)
            for k in kids:
                if k["componentData"]["name"].strip() == n:
                    k["componentData"]["type"] = nd["key"]


def patch(path):
    folder = os.path.dirname(path)
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    split_shared_toggles(doc, folder)
    defs = {d["key"]: d for d in doc["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    # nom de chaque bouton sur la page -> sa définition
    users = {}
    for d in defs.values():
        for k in d["value"]["componentsData"]:
            t = k["componentData"]["type"]
            if t in defs and t.startswith("shToggle"):
                users.setdefault(t, set()).add(k["componentData"]["name"].strip())
    removed = drawn = 0
    # noms des instances de la page pour chaque définition (tous types de contrôle)
    users_all = {}
    for d in defs.values():
        for k in d["value"]["componentsData"]:
            t = k["componentData"]["type"]
            if t in defs:
                users_all.setdefault(t, set()).add(k["componentData"]["name"].strip())
    for key, d in defs.items():
        kids = d["value"]["componentsData"]
        if key.startswith("sh") and not key.startswith("shToggle") and users_all.get(key, set()) & set(KEEP_VALUE):
            keep = [k for k in kids if not is_label(k, ("Name",))]
            removed += len(kids) - len(keep)
            inst = None                                  # taille de la fenêtre sur la page
            for d2 in defs.values():
                for k2 in d2["value"]["componentsData"]:
                    if k2["componentData"]["type"] == key and k2["componentData"]["name"].strip() in KEEP_VALUE:
                        inst = k2["bounds"]["bounds"].split()
            for k in keep:
                if is_label(k, ("Value",)):
                    ts = k["componentData"]["data"]["textStyle"]
                    ts["colour"] = VALUE_COLOUR
                    ts["font"]["height"] = VALUE_SIZE
                    ts["justification"] = "horizontallyCentred verticallyCentred"
                    if inst and len(inst) == 4:          # le texte occupe toute la fenêtre, centré
                        k["bounds"]["bounds"] = "0 0 %s %s" % (inst[2], inst[3])
            d["value"]["componentsData"] = keep
            print("skin_post: « %s » : valeur conservée (%d composant(s))" % (sorted(users_all[key])[0], len(keep)))
            continue
        if key.startswith("shSlider"):
            keep = [k for k in kids if not is_label(k, ("Name", "Value"))]
            removed += len(kids) - len(keep)
            d["value"]["componentsData"] = keep
        elif key.startswith("shToggle"):
            names = sorted(n for n in users.get(key, ()) if n)
            btn = next((k for k in kids if k["componentData"]["type"] == "Button"), None)
            if btn and len(names) == 1 and names[0] in WHITE_LIT:
                paths = [os.path.join(folder, btn["componentData"]["data"][f]) for f in ("offImage", "onImage")]
                lit = max(paths, key=mean_sat)
                if mean_sat(lit) > 40:
                    make_white(lit)
                    print("skin_post: « %s » : image allumée convertie en blanc (%s)" % (names[0], os.path.basename(lit)))
            if btn and len(names) == 1 and names[0] in NO_TEXT:
                print("skin_post: « %s » : sans texte (LED seule)" % names[0])
            elif btn and len(names) == 1:
                for img in (btn["componentData"]["data"]["offImage"], btn["componentData"]["data"]["onImage"]):
                    src, size = draw_label(os.path.join(folder, img), names[0])
                drawn += 1
                print("skin_post: texte « %s » écrit dans %s (police : %s, taille %d)" % (names[0], key, src, size))
            elif btn:
                print("skin_post: ATTENTION %s est partagé par %s : texte non écrit" % (key, names))
            keep = [k for k in kids if not is_label(k, ("Name", "Value"))]
            removed += len(kids) - len(keep)
            d["value"]["componentsData"] = keep
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=4, ensure_ascii=False)
    print("skin_post: %s : %d texte(s) de la MPC retiré(s), %d bouton(s) écrit(s)" % (path, removed, drawn))


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    found = 0
    for dp, _, files in os.walk(root):
        if "TUI.json" in files:
            patch(os.path.join(dp, "TUI.json"))
            found += 1
    if not found:
        sys.exit("skin_post: TUI.json introuvable sous " + root)


main()
