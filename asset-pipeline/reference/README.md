# reference/

외부 게임(Pillars of Eternity, Divinity: Original Sin 1, 2)의 공식 스크린샷에서 글로 된 규칙을 뽑기 위한 자료 폴더다.
분석 결과는 [`docs/REFERENCE_BRIEF.md`](../docs/REFERENCE_BRIEF.md).

- `sources.json`: 출처 URL, 용도 태그, 한 줄 메모 (커밋됨)
- `local/`: 내려받은 이미지 (**git 무시, 커밋 금지**)

```
python scripts/fetch_references.py                  # 전부 (홍보 문구 이미지 제외 30장)
python scripts/fetch_references.py --only unit monster   # 태그로 골라서
```

이미지는 저작권물이다. 눈으로 보고 글 규칙을 뽑거나, 생성 도구의 스타일 입력으로만 쓴다.
스타일 입력은 `create_image_pro` 의 `style_image_url` + `style_copy: ["color_palette", "shading"]` 뿐이다. 앵커, `spec.references`, 인페인트/img2img 입력으로는 넘기지 않는다 (`CLAUDE.md` 규칙 13).
