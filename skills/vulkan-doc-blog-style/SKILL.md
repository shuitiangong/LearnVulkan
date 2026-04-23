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
  - “这说明……”
  - “目标：……”
  - “讨论的重点应该放在……”
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
     - the step’s purpose, written as normal prose rather than a `目标：` label
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
- Avoid prose that reads like the author’s scratchpad or reasoning trace. Do not write phrases such as:
  - “这说明……”
  - “讨论的重点应该放在……”
  - “有助于把……区分开”
  - “下面这段代码只是……”
  - “目标：……”
  - “接下来……”
  Prefer direct blog prose such as “数据路径可以概括为……”, “这行代码负责……”, “对应流程是……”。
- Do not add a standalone "完整示例" section unless explicitly requested by the user.

## Code and Implementation Accuracy

- Keep documentation synchronized with the current codebase. Inspect relevant source files before describing project-specific behavior.
- Distinguish resource creation from data upload. For example, constructing a `Buffer` may create `vk::Buffer`, allocate `vk::DeviceMemory`, and bind memory, but it does not mean vertex/index/uniform data has already been written.
- When describing a wrapper/helper’s internal behavior, include the relevant implementation snippet instead of only saying what it does. Examples:
  - If saying `WriteData` uses `memcpy`, show the `Buffer::WriteData` implementation or the focused part that performs the copy.
  - If saying non-coherent memory triggers `flushMappedMemoryRanges`, show the `flushIfNeeded` implementation or the focused part that builds `vk::MappedMemoryRange`.
- Explain non-obvious C++ details next to the snippet. Example: explain that `static_cast<char*>(map_) + offset` converts `void*` to a byte-addressable pointer so `offset` is interpreted in bytes.
- Avoid stale snippets that access private internals directly after the code has been refactored. Prefer public interfaces such as `buffer.WriteData(...)` when the project exposes them.

## Quality Checklist

Before finalizing, verify:

- Heading numbers are consistent.
- Default order is `概念 -> 流程`.
- Every process step names its API/interface.
- Code snippets are embedded near the corresponding API explanation.
- No meta narration remains.
- No scratchpad-style phrases remain, especially “这说明 / 目标：/ 讨论重点 / 有助于 / 下面这段”.
- Project-specific descriptions match the current source code.
- If an implementation detail is mentioned, the relevant code snippet is shown nearby.
- The level of detail is sufficient for study/blog use.

## Reusable Skeleton

```md
# 1 <Topic>

## 1.1 概念

## 1.2 流程

### 1.2.1 <Step A>

### 1.2.2 <Step B>
```
