---
name: modern-game-programming-check
description: Use this skill when creating, modifying, refactoring, or reviewing game code. The goal is to keep **dependencies controlled, changes isolated, interfaces meaningful, ownership explicit, and complexity proportional to the problem.** 
---

## Core Principles

### 1. Control Dependencies

- Keep dependencies explicit and directional.
- Avoid circular dependencies.
- Prefer stable abstractions over volatile implementations when appropriate.
- Prevent platform, engine, renderer, OS, and third-party implementation details from unnecessarily spreading through the codebase.
- Minimize the number of subsystems a feature depends on.

### 2. Isolate Change

Design boundaries around **likely sources of change**.

Before introducing a dependency, ask:

> If this implementation changes, what else must change?

Prefer designs where implementation changes remain local.

Do not introduce abstractions merely because future changes are possible. Introduce them when they meaningfully reduce coupling or isolate change.

### 3. Use Interfaces as Contracts

Use an interface when it provides a meaningful boundary between a consumer and an implementation.

Good reasons include:

- implementations may be replaced;
- platform or backend implementations differ;
- testing requires substitution;
- implementation details should remain hidden;
- independent evolution of the consumer and implementation is valuable.

Interfaces should expose **capabilities and contracts**, not implementation details.

Avoid:

- interfaces created solely for convention;
- large interfaces with unrelated responsibilities;
- interfaces that leak the underlying implementation;
- abstraction layers that do not reduce meaningful coupling.

The goal is not to prevent interfaces from changing.

The goal is:

> **Changing an implementation should require minimal changes to its consumers.**

### 4. Keep Responsibilities Focused

- Give each class and subsystem a clear responsibility.
- Keep unrelated responsibilities separate when they have different reasons to change.
- Keep subsystem boundaries explicit.
- Avoid god objects and dependency-sink classes such as oversized `Manager`, `Utils`, or `Global` classes.
- Prefer composition when it produces clearer and more independent responsibilities.

### 5. Make Ownership Explicit

For every important object or resource, know:

```text
Who creates it?
Who owns it?
How long does it live?
Who destroys it?
```

Prefer, in general:

```text
Value
→ Reference
→ Unique ownership
→ Shared ownership
```

Use shared ownership only when shared ownership is actually required.

Prefer RAII and deterministic lifetime management.

### 6. Minimize Abstraction

Choose the simplest mechanism that satisfies the requirements.

Do not introduce:

- unnecessary interfaces;
- unnecessary wrappers;
- unnecessary event systems;
- unnecessary dependency injection;
- unnecessary managers;
- unnecessary inheritance hierarchies.

Architectural complexity must have a concrete benefit.

### 7. Respect Existing Architecture

Before changing code:

1. Inspect the existing structure.
2. Identify existing abstractions and subsystem boundaries.
3. Reuse existing mechanisms when appropriate.
4. Avoid introducing a competing architecture without a clear reason.

Do not redesign the architecture merely to implement a small feature.

### 8. Performance Requires Evidence

Do not assume that an architectural pattern is faster.

For performance-sensitive code:

```text
Measure
→ Identify the bottleneck
→ Change the relevant design
→ Measure again
```

Use data-oriented techniques, ECS, pooling, caching, or specialized memory layouts when the workload justifies them.

Do not sacrifice clarity for hypothetical performance.

---

## Implementation Workflow

When implementing a feature or refactoring:

### Step 1 — Inspect

Understand:

- existing dependencies;
- subsystem boundaries;
- ownership;
- relevant interfaces;
- build dependencies;
- established conventions.

### Step 2 — Map Dependencies

Identify what the new or modified code actually needs.

Remove dependencies that are not essential.

### Step 3 — Identify the Change Boundary

Determine what is likely to change independently.

Ask:

> What should be able to change without forcing unrelated code to change?

Use that answer to guide subsystem and interface boundaries.

### Step 4 — Choose the Simplest Boundary

Consider, in order:

```text
Concrete implementation
→ Reference/value
→ Small interface
→ Adapter
→ Event/message
```

Use the simplest option that adequately controls coupling.

Do not escalate to a more complex mechanism without a reason.

### Step 5 — Verify Ownership

Confirm creation, ownership, lifetime, and destruction.

Avoid introducing ambiguous ownership.

### Step 6 — Implement

Make the smallest architectural change that solves the problem cleanly.

### Step 7 — Review

Before finalizing, check:

```text
[ ] Dependencies remain directional.
[ ] No unnecessary coupling was introduced.
[ ] The change boundary is appropriate.
[ ] Interfaces have a concrete purpose.
[ ] Interfaces expose contracts, not implementations.
[ ] Ownership and lifetime are explicit.
[ ] Responsibilities remain focused.
[ ] Third-party details do not leak unnecessarily.
[ ] No unnecessary abstraction was introduced.
[ ] Performance decisions are evidence-based.
```

---

## Decision Rule

When multiple designs are viable, prefer the one that:

1. reduces coupling;
2. isolates likely changes;
3. makes ownership explicit;
4. has fewer unnecessary dependencies;
5. is easier to understand and test;
6. introduces the least complexity.

### Final Principle

> **Control dependencies. Isolate change. Define clear contracts. Make ownership explicit. Prefer the simplest design that preserves these properties.**
