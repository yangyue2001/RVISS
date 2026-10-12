# Task contract

Use this template for a task that cannot be safely completed in one small edit. Keep it short enough to review before implementation.

```markdown
# <task id>: <short title>

Status: pending | in_progress | verified | blocked

## Objective

- Trigger:
- Observable result:

## Non-goals

-

## Scope

- Allowed files/modules:
- Interfaces that may change:
- Inputs and reference implementations:

## Constraints

- Compatibility/API/ABI:
- Runtime, resource, security, or performance limits:
- Required tools and versions:
- Permission or external-side-effect boundary:

## Acceptance

- [ ] <command or reproducible check>
- [ ] <behavioral assertion>
- [ ] <review or artifact invariant>

## Assumptions and open decisions

- Assumption:
- Open question (only if blocking):

## Slices

1. [ ] <one behavior> — verify with `<command>`; stop if `<condition>`.

## Evidence

- Commands actually run:
- First failure or baseline failure:
- Artifacts/logs/checkpoint:
- Remaining risk:
```

Do not add a schedule, an abstraction roadmap, or a test matrix unless the objective needs it. A contract is an executable boundary, not a status report written after the work.
