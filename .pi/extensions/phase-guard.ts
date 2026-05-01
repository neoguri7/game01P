/**
 * phase-guard.ts
 *
 * Enforces the Plan → Code → Verify loop at the tool level.
 * Reads PLAN.md's YAML frontmatter (`phase: plan|code|verify`) on each turn
 * and restricts the active tool set accordingly via pi.setActiveTools.
 *
 * Phase → allowed built-in tools:
 *   plan   : read, grep, find, ls, bash      (no write/edit — planning only)
 *   code   : read, grep, find, ls, bash, edit, write   (full set)
 *   verify : read, grep, find, ls, bash      (no write/edit — observation only)
 *
 * Also exposes:
 *   /phase plan|code|verify   manual override (useful if the model forgot
 *                             to flip the frontmatter)
 *
 * The footer shows "phase: X" so the current constraint is always visible.
 *
 * Install: drop into .pi/extensions/phase-guard.ts
 */

import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";

type Phase = "plan" | "code" | "verify";

const PHASE_TOOLS: Record<Phase, string[]> = {
	plan: ["read", "grep", "find", "ls", "bash"],
	code: ["read", "grep", "find", "ls", "bash", "edit", "write"],
	verify: ["read", "grep", "find", "ls", "bash"],
};

const FRONTMATTER_RE = /^---\s*\n([\s\S]*?)\n---/;
const PHASE_RE = /^phase\s*:\s*(plan|code|verify)\s*$/m;

async function readPhase(cwd: string): Promise<Phase | null> {
	try {
		const content = await readFile(join(cwd, "PLAN.md"), "utf8");
		const fm = content.match(FRONTMATTER_RE);
		if (!fm) return null;
		const p = fm[1].match(PHASE_RE);
		return p ? (p[1] as Phase) : null;
	} catch {
		return null;
	}
}

async function writePhase(cwd: string, phase: Phase): Promise<boolean> {
	try {
		const path = join(cwd, "PLAN.md");
		const content = await readFile(path, "utf8");
		const fm = content.match(FRONTMATTER_RE);
		let next: string;
		if (fm) {
			const body = fm[1];
			const newBody = PHASE_RE.test(body)
				? body.replace(PHASE_RE, `phase: ${phase}`)
				: `${body}\nphase: ${phase}`;
			next = content.replace(FRONTMATTER_RE, `---\n${newBody}\n---`);
		} else {
			next = `---\nphase: ${phase}\n---\n\n${content}`;
		}
		await writeFile(path, next, "utf8");
		return true;
	} catch {
		return false;
	}
}

export default function (pi: ExtensionAPI) {
	let currentPhase: Phase | null = null;

	async function applyIfChanged(ctx: any) {
		const phase = await readPhase(ctx.cwd);
		if (!phase) {
			ctx.ui?.setStatus?.("phase-guard", "phase: ? (PLAN.md missing or malformed)");
			return;
		}
		if (phase !== currentPhase) {
			currentPhase = phase;
			pi.setActiveTools(PHASE_TOOLS[phase]);
			ctx.ui?.setStatus?.("phase-guard", `phase: ${phase}`);
			ctx.ui?.notify?.(`Phase → ${phase}`, "info");
		}
	}

	pi.on("session_start", async (_event, ctx) => {
		await applyIfChanged(ctx);
	});

	// After each assistant turn, the model may have rewritten PLAN.md and
	// flipped the phase. Re-read and reapply if so.
	pi.on("turn_end", async (_event, ctx) => {
		await applyIfChanged(ctx);
	});

	pi.registerCommand("phase", {
		description: "Set phase manually: /phase plan|code|verify",
		handler: async (args, ctx) => {
			const p = (args || "").trim().toLowerCase() as Phase;
			if (p !== "plan" && p !== "code" && p !== "verify") {
				ctx.ui?.notify?.("Usage: /phase plan|code|verify", "warning");
				return;
			}
			const ok = await writePhase(ctx.cwd, p);
			if (!ok) {
				ctx.ui?.notify?.("Failed to write PLAN.md — does it exist at the project root?", "error");
				return;
			}
			currentPhase = p;
			pi.setActiveTools(PHASE_TOOLS[p]);
			ctx.ui?.setStatus?.("phase-guard", `phase: ${p}`);
			ctx.ui?.notify?.(`Phase set to ${p}`, "info");
		},
	});
}
