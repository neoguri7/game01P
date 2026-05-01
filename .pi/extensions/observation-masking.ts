/**
 * observation-masking.ts
 *
 * Reduces input token cost by trimming old tool-result outputs that are
 * no longer the agent's current focus. Based on the JetBrains 2025 study
 * "The Complexity Trap" (arXiv 2508.21433), which found observation masking
 * halves cost (−52.7%) on SWE-bench Verified while matching or slightly
 * exceeding the solve rate of LLM-based summarization.
 *
 * Strategy (intentionally simple):
 *  - Keep the last N tool results in full (fresh working state).
 *  - Older tool-result messages: keep first 3 lines + a marker noting size.
 *  - Protect results that reference PLAN.md / CODEMAP.md / AGENTS.md;
 *    those are the externalized working memory and must stay readable.
 *  - Never touch assistant/user messages. Only toolResult.
 *
 * Install: drop into .pi/extensions/observation-masking.ts
 * Tune the two constants below for your session length.
 */

import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";

const KEEP_RECENT_RESULTS = 4; // last N tool results stay untouched
const MAX_OLDER_RESULT_BYTES = 500; // older results trimmed if above this
const PROTECTED_MARKERS = ["PLAN.md", "CODEMAP.md", "AGENTS.md"];

export default function (pi: ExtensionAPI) {
	pi.on("context", async (event, _ctx) => {
		const messages = event.messages as any[];

		// Collect indices of toolResult messages in order.
		const toolResultIdxs: number[] = [];
		for (let i = 0; i < messages.length; i++) {
			if (messages[i]?.role === "toolResult") toolResultIdxs.push(i);
		}

		if (toolResultIdxs.length <= KEEP_RECENT_RESULTS) {
			return; // nothing old enough to mask
		}

		const toMask = new Set(toolResultIdxs.slice(0, -KEEP_RECENT_RESULTS));

		const newMessages = messages.map((m: any, i: number) => {
			if (!toMask.has(i)) return m;
			if (m?.role !== "toolResult") return m;

			const currentText = stringifyContent(m.content);

			// Protect results that carry externalized working memory.
			if (PROTECTED_MARKERS.some((p) => currentText.includes(p))) {
				return m;
			}

			if (currentText.length <= MAX_OLDER_RESULT_BYTES) return m;

			// Keep the first 3 lines so the LLM can still tell roughly what this was.
			const lines = currentText.split("\n");
			const header = lines.slice(0, 3).join("\n");
			const toolName = m.toolName ?? "tool";
			const marker = `\n[masked — ${currentText.length} bytes of ${toolName} output trimmed; re-run the tool if the full content is needed]`;

			return {
				...m,
				content: [{ type: "text", text: header + marker }],
			};
		});

		return { messages: newMessages };
	});
}

function stringifyContent(content: unknown): string {
	if (typeof content === "string") return content;
	if (!Array.isArray(content)) return "";
	return content
		.map((c: any) => {
			if (typeof c === "string") return c;
			if (c && typeof c === "object" && c.type === "text" && typeof c.text === "string") {
				return c.text;
			}
			return "";
		})
		.join("\n");
}
