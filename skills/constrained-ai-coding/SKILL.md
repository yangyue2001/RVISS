---
name: constrained-ai-coding
description: Turn software requests into scoped, verifiable, and resumable implementation loops for existing repositories; use for features, bug fixes, refactors, reviews, and multi-step agent coding.
metadata:
  short-description: "受约束、可验证、可恢复的 AI Coding 协作流程"
---

# Constrained AI Coding

## Purpose

Use this skill when a user wants an agent to change, debug, review, or plan software in a real project. Convert the request into a small engineering contract, make the smallest useful change, and close the loop with evidence. The user owns the goal, scope, tradeoffs, and final decisions; the agent searches and implements locally; repository rules, tests, and scripts provide the control layer.

This skill is project-agnostic. Do not import a previous project's language, directory layout, architecture, or validation commands into a new project. Extract reusable constraints from the current request and verify them against the current repository.

## First pass: establish the project

Before editing, inspect only the context needed for the request:

1. Find and read applicable `AGENTS.md`, `CONTRIBUTING`, `README`, build manifests, test commands, and local rules. More specific rules override broader ones.
2. Locate the implementation entry point, neighboring code, callers, configuration, and the closest existing tests or examples.
3. Check the working tree before changing it. Preserve user changes and distinguish a pre-existing failure from a regression introduced by this task.
4. For a referenced external directory, document whether it is an implementation dependency, a read-only reference, or an input artifact. Do not copy its conventions blindly.

Treat source, tests, tool output, and fixed specifications as evidence. Treat README completion claims, generated stubs, exported symbols, and old logs as hypotheses until checked.

## Convert the request into a contract

Write a compact internal contract before implementation. For a task with more than one meaningful step, save it in the project's existing task/planning location; if none exists, use a project-local checkpoint such as `docs/tasks/<id>.md` only when that helps resume or review. Use [task-contract.md](references/task-contract.md).

The contract must state:

- the observable objective and the user-visible trigger;
- non-goals and the files or interfaces outside scope;
- relevant constraints: versions, ABI/API compatibility, performance, security, platform, permissions, and data safety;
- acceptance checks with concrete commands, assertions, or expected behavior;
- assumptions and unresolved choices.

Expose materially different interpretations. Ask a focused question only when choosing an interpretation would change the external behavior, data safety, compatibility, or scope. Otherwise state the assumption and proceed. Do not silently add configuration, abstractions, cleanup, or features.

## Select a working mode

Choose the narrowest mode that fits the request:

- **Bug fix:** reproduce the failure or create the smallest reliable reproducer, then make the smallest fix and keep the regression check.
- **Feature:** define the smallest end-to-end behavior, implement one vertical slice, then extend only after it passes.
- **Refactor:** preserve behavior, move one seam at a time, and compare relevant tests or observable outputs before and after.
- **Review:** prioritize correctness, regression risk, security, data loss, compatibility, and missing tests; report findings with file, location, trigger, and impact.
- **Design/research:** inspect the current system and alternatives, record tradeoffs and a decision boundary, and avoid speculative implementation.
- **Build/release/integration:** pin inputs and environment, separate artifact validation from product validation, and record reproducible commands and hashes when useful.

For multi-module work, split the mode into slices. Each slice has one behavior, a bounded diff, a verification command, and a stop condition.

## Implement with a tight loop

For every slice:

1. State the slice and the files likely to change.
2. Read the relevant call graph and data/control flow before editing.
3. Implement the smallest coherent change. Preserve existing public interfaces unless the contract explicitly changes them.
4. Run the most direct check immediately: a reproducer, focused unit test, type check, linter, build target, or deterministic runtime check.
5. If it fails, preserve the first useful failure, classify it as baseline, regression, environment, or unknown, and fix the cause. Do not weaken assertions, widen skips, or alter expected output just to obtain a pass.
6. Re-read the diff and callers. Check error paths, boundary states, resource cleanup, compatibility, and configuration consistency.
7. Update the task checkpoint with completed work, actual evidence, remaining risk, and the next smallest step.

Use [verification-and-state.md](references/verification-and-state.md) for the evidence and checkpoint format. Keep observers, logging, tracing, benchmarks, and checkpoint code from changing the behavior under test unless the task explicitly requires it.

## Context and harness discipline

Give the agent the smallest context that determines the current decision: relevant interfaces, nearby implementations, constraints, current failure, and verified intermediate state. Use summaries and checkpoints for long tasks; do not repeatedly paste the entire repository or chat history.

A prompt controls one reasoning turn. A harness or task runner should control ordering, validation, retries, timeouts, artifact capture, and resume state. If the repository already has scripts for these concerns, use them rather than recreating equivalent shell logic.

Do not parallelize dependent edits or tests. Parallelize only independent read-only discovery or independent checks. Keep external side effects, migrations, deployments, publication, destructive cleanup, and ABI changes behind the project's authorization and review boundary.

## Completion standard

A task is complete only when its stated acceptance checks pass, the relevant diff has been reviewed, and the state is recorded. Report:

- behavior changed and why;
- files or interfaces changed;
- commands actually run and their results;
- pre-existing failures, skipped checks, unavailable tools, or unverified assumptions;
- remaining risks and the next task, if the larger objective is unfinished.

Never equate compilation, a log banner, a generated file, or a model explanation with behavioral correctness. Do not claim a larger milestone from a narrower passing slice.

## Reusable user prompt

Users can invoke this skill with a request such as:

> 使用 `$constrained-ai-coding`。先读取当前项目规则和相关代码，
> 把我的需求整理成目标、非目标、约束、验收标准和假设；
> 如果没有阻塞性歧义就直接执行一个最小可验证切片。
> 实际运行相关检查，保存任务状态，最后报告变更、证据、风险和下一步。

For a new project, append the project's technology stack, reference paths, forbidden changes, required artifacts, and exact acceptance commands. Do not append a workflow copied from another project without revalidating it.

## Project-specific adaptation

When this skill is used inside a repository, read that repository's own rules and documents after loading this file. For example, a project may define a specific task directory, language conventions, build wrapper, or acceptance harness. Those local requirements refine this workflow; they do not turn unverified historical claims into facts.
