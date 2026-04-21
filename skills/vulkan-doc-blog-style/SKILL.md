---
name: vulkan-doc-blog-style
description: Write Vulkan learning notes/blog posts in Chinese with strict numbered structure and blog-ready tone. Use when the user asks for technical notes, chapter drafting, blog polishing, or documentation standardization for Vulkan topics. Enforce concept-first explanation, then process walkthrough with API explanations and inline code snippets. Do not add standalone full-example sections unless the user explicitly asks.
---

# Vulkan Doc Blog Style

## Workflow

1. Identify the target topic and scope.
2. Draft concept sections first with sufficient depth (avoid over-brief summaries).
3. Draft process sections with step-by-step API explanation and inline snippets.
4. Polish structure, numbering, and prose quality.

## Output Structure Rules

- Use numbered headings.
- Prefer `1`, `1.1`, `1.1.1` hierarchy.
- If the user explicitly wants multiple top-level parts, split into multiple H1 sections (example: `# 1 Vulkan Instance`, `# 2 验证层`, `# 3 DebugMessenger`).
- Avoid meta text such as:
  - “下面按……来讲”
  - “这里穿插……”
  - “接下来我会……”
- Write direct technical prose suitable for a public blog.

## Required Content Pattern

For each topic, follow this sequence:

1. **Concept**
   - What it is
   - Why it exists
   - Core responsibilities
   - Common pitfalls or misunderstandings
   - Keep explanations detailed enough for study notes, not one-line summaries.

2. **Process**
   - Step-by-step flow
   - For each step include:
     - goal
     - key API/interface
     - minimal code snippet
   - Keep snippets focused and explain only relevant parameters.
   - Place snippets directly under the related API/interface explanation.

## Style Requirements

- Language: Chinese (technical terms may stay in English).
- Tone: objective, clear, blog-ready; prefer detailed technical explanation over brief bullet-only output.
- Prefer `vk::`/Vulkan-Hpp style when code style is not specified.
- Align examples with the user’s codebase naming when context is available.
- Prefer neutral blog narration; avoid second-person phrasing such as “你的项目中 / 你当前工程 / 你当前代码”, and use forms like “项目中 / 当前工程 / 当前代码” instead.
- Do not include conversational filler or private planning notes.
- Do not add a standalone "完整示例" section unless explicitly requested by the user.

## Quality Checklist

Before finalizing, verify:

- Heading numbers are consistent.
- Default order is `概念 -> 流程`.
- Every process step names its API/interface.
- Code snippets are embedded near the corresponding API explanation.
- No meta narration remains.
- The level of detail is sufficient for study/blog use.

## Reusable Skeleton

```md
# 1 <Topic>

## 1.1 概念

## 1.2 流程

### 1.2.1 <Step A>

### 1.2.2 <Step B>
```
