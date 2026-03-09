#!/usr/bin/env python3
"""Send UI screenshots to GPT for visual UX review.

Usage:
    python3 gpt_ui_review.py --images img1.png img2.png \
        --description "Weather sandbox: atmospheric simulation..." \
        --focus "layout,colors,readability"

Requires: OPENAI_API_KEY in .env (project root) or environment.
Dependencies: openai (pip install openai)
"""

import argparse
import base64
import json
import os
import sys
from pathlib import Path


def load_env():
    """Load OPENAI_API_KEY from .env files or environment."""
    if os.environ.get("OPENAI_API_KEY"):
        return
    cwd = Path.cwd()
    for d in [cwd, *cwd.parents]:
        env_path = d / ".env"
        if env_path.exists():
            for line in env_path.read_text().splitlines():
                line = line.strip()
                if line.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                key, value = key.strip(), value.strip()
                if key == "OPENAI_API_KEY" and value:
                    os.environ["OPENAI_API_KEY"] = value
                    return
    print("ERROR: OPENAI_API_KEY not found in .env or environment", file=sys.stderr)
    sys.exit(1)


def encode_image(path: str) -> str:
    with open(path, "rb") as f:
        return base64.b64encode(f.read()).decode("utf-8")


FOCUS_DESCRIPTIONS = {
    "layout": "Spatial arrangement of panels, legends, buttons, and overlays. Is information well-organized? Is screen space used efficiently?",
    "colors": "Color schemes, contrast, accessibility. Are colors perceptually uniform? Do overlays have good contrast against the background? Any accessibility concerns?",
    "readability": "Text size, font clarity, information density. Can all text be read at normal viewing distance? Is information dense but not overwhelming?",
    "overlays": "Overlay visualization quality. Are color ramps intuitive? Does the data-to-visual mapping make sense? Are transitions between values smooth?",
    "controls": "Discoverability of controls. Are key bindings shown? Are buttons labeled? Can a new user figure out the interface?",
    "consistency": "Visual consistency across modes and states. Do overlays use consistent color language? Are panel positions stable?",
    "feel": "Overall aesthetic and polish. Does it match the stated target feel? Does it look professional or rough?",
}


def build_review_prompt(description, focus_areas, previous_rating=None):
    """Build the review prompt with optional focus areas."""
    focus_text = ""
    if focus_areas:
        areas = [a.strip() for a in focus_areas.split(",")]
        focus_text = "\n## Focus Areas\nPay special attention to:\n"
        for area in areas:
            desc = FOCUS_DESCRIPTIONS.get(area, f"Review the '{area}' aspect of the UI.")
            focus_text += f"- **{area.title()}**: {desc}\n"

    previous_text = ""
    if previous_rating:
        previous_text = f"\n## Previous Rating\nThe last review rated this UI {previous_rating}/10. Evaluate whether the changes improved things.\n"

    return (
        "You are an expert UI/UX designer reviewing an interactive application's interface. "
        "These are screenshots of a software tool's UI in different states.\n\n"
        f"## Application Description\n{description}\n"
        f"{focus_text}"
        f"{previous_text}"
        "\n## Your Task\n"
        "Analyze the UI screenshots for:\n"
        "1. **Layout & Organization** - Is information well-arranged? Is screen space used well?\n"
        "2. **Visual Clarity** - Can all elements be read/understood at a glance?\n"
        "3. **Color & Contrast** - Are colors effective? Good contrast? Accessible?\n"
        "4. **Information Density** - Too sparse? Too cluttered? Right balance?\n"
        "5. **Consistency** - Are visual patterns consistent across states/modes?\n"
        "6. **Polish** - Does it feel professional? Any rough edges?\n"
        "7. **Specific Issues** - List every visual problem with its location in the screenshot\n"
        "8. **What Works Well** - Acknowledge successful UI decisions\n"
        "9. **Concrete Improvements** - For each issue, suggest a specific fix with details "
        "(colors as hex values, pixel sizes, layout changes)\n\n"
        "Rate overall UI quality 1-10 and list the top 3 most impactful improvements.\n"
        "Be specific and constructive. Reference screenshot locations precisely."
    )


def build_content(image_paths, description, focus_areas=None, previous_rating=None):
    """Build multimodal content array with text + images."""
    prompt = build_review_prompt(description, focus_areas, previous_rating)
    content = [{"type": "text", "text": prompt}]
    for path in image_paths:
        if not os.path.exists(path):
            print(f"WARNING: Image not found: {path}", file=sys.stderr)
            continue
        b64 = encode_image(path)
        ext = Path(path).suffix.lower()
        media_type = {
            ".png": "image/png",
            ".jpg": "image/jpeg",
            ".jpeg": "image/jpeg",
            ".bmp": "image/bmp",
        }.get(ext, "image/png")
        content.append({
            "type": "image_url",
            "image_url": {"url": f"data:{media_type};base64,{b64}", "detail": "high"},
        })
        content.append({"type": "text", "text": f"[Screenshot: {Path(path).name}]"})
    return content


def review_ui(image_paths, description, focus_areas=None, previous_rating=None, model="gpt-5.2"):
    """Send screenshots + description to GPT and return the review text."""
    load_env()

    from openai import OpenAI
    client = OpenAI()

    content = build_content(image_paths, description, focus_areas, previous_rating)
    response = client.chat.completions.create(
        model=model,
        messages=[{"role": "user", "content": content}],
        max_completion_tokens=4000,
    )
    return response.choices[0].message.content


def main():
    parser = argparse.ArgumentParser(description="GPT visual review of UI screenshots")
    parser.add_argument("--images", nargs="+", required=True, help="Screenshot file paths")
    parser.add_argument("--description", required=True,
                        help="Description of the module/sandbox and target UI feel")
    parser.add_argument("--focus", default=None,
                        help="Comma-separated focus areas: layout,colors,readability,overlays,controls,consistency,feel")
    parser.add_argument("--previous-rating", type=int, default=None,
                        help="Rating from previous review round (1-10)")
    parser.add_argument("--model", default="gpt-5.2", help="GPT model to use (default: gpt-5.2)")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    args = parser.parse_args()

    result = review_ui(args.images, args.description, args.focus, args.previous_rating, args.model)

    if args.json:
        print(json.dumps({
            "review": result,
            "model": args.model,
            "images": args.images,
            "focus": args.focus,
        }))
    else:
        print(result)


if __name__ == "__main__":
    main()
