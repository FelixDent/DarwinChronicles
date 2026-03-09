#!/usr/bin/env python3
"""Send procedural generation output images to GPT for visual review.

Usage:
    python3 gpt_review.py --images img1.png img2.png \
        --description "Tectonic terrain gen with 9-stage pipeline..." \
        --model gpt-5.2

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
    # Search for .env in current dir and parents
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


REVIEW_PROMPT = (
    "You are an expert in procedural terrain/world generation for games and simulations. "
    "Review these generated output images critically.\n\n"
    "{description}\n\n"
    "## Your Task\n"
    "Analyze each image for:\n"
    "1. **Realism** - Does it look like plausible geography? Or are there obvious artifacts?\n"
    "2. **Variety** - Do different seeds produce meaningfully different worlds?\n"
    "3. **Specific Issues** - List every visual problem you see (e.g., mountains hugging coasts, "
    "speckle noise, uniform textures, unrealistic lake shapes, missing features)\n"
    "4. **What works well** - Acknowledge successful aspects\n"
    "5. **Concrete improvements** - For each issue, suggest a specific algorithmic fix with "
    "parameter ranges where possible\n\n"
    "Be precise and harsh. The goal is photorealistic procedural terrain. "
    "Rate overall quality 1-10 and list the top 3 most impactful improvements."
)


def _build_content(image_paths, description):
    """Build multimodal content array with text + images."""
    content = [{"type": "text", "text": REVIEW_PROMPT.format(description=f"## Generation Approach\n{description}")}]
    for path in image_paths:
        if not os.path.exists(path):
            print(f"WARNING: Image not found: {path}", file=sys.stderr)
            continue
        b64 = encode_image(path)
        ext = Path(path).suffix.lower()
        media_type = "image/png" if ext == ".png" else "image/jpeg" if ext in (".jpg", ".jpeg") else "image/png"
        content.append({
            "type": "image_url",
            "image_url": {"url": f"data:{media_type};base64,{b64}", "detail": "high"},
        })
        content.append({"type": "text", "text": f"[Image: {Path(path).name}]"})
    return content


def review_images(image_paths, description, model="gpt-5.2"):
    """Send images + description to GPT and return the review text."""
    load_env()

    from openai import OpenAI
    client = OpenAI()

    content = _build_content(image_paths, description)
    response = client.chat.completions.create(
        model=model,
        messages=[{"role": "user", "content": content}],
        max_completion_tokens=4000,
    )
    return response.choices[0].message.content


def main():
    parser = argparse.ArgumentParser(description="GPT visual review of procgen outputs")
    parser.add_argument("--images", nargs="+", required=True, help="Image file paths")
    parser.add_argument("--description", required=True, help="Description of the generation approach")
    parser.add_argument("--model", default="gpt-5.2", help="GPT model to use (default: gpt-5.2)")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    args = parser.parse_args()

    result = review_images(args.images, args.description, args.model)

    if args.json:
        print(json.dumps({"review": result, "model": args.model, "images": args.images}))
    else:
        print(result)


if __name__ == "__main__":
    main()
