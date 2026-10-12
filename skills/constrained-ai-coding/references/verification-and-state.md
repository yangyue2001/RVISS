# Verification and state

## Verification order

Prefer the cheapest check that can falsify the current slice, then broaden only when the result justifies it:

1. Reproducer or focused assertion.
2. Direct unit/integration check for the changed path.
3. Static checks and build of the affected target.
4. Relevant regression suite or runtime smoke.
5. Broader checks required by the repository or acceptance contract.

Capture the exact command, exit code, important output, and artifact/log path. A command that was proposed but not run is not evidence. If a check cannot run, state why and whether the result remains unverified.

## Failure classification

- **Baseline:** reproduced before the change or caused by the repository/environment independently of the slice.
- **Regression:** introduced by the current change; stop and fix before proceeding.
- **Environment:** missing tool, permission, dependency, timeout, or external service; preserve the command and failure.
- **Unknown:** insufficient evidence; reduce to a smaller reproducer before changing more code.

Never make a failing check pass by changing the expected result, hiding the failure, increasing a skip list, or deleting unrelated tests.

## Checkpoint update

After each meaningful slice, record:

```text
Status: <pending/in_progress/verified/blocked>
Completed: <observable behavior>
Evidence: <commands, exit codes, artifact/log paths>
First failure: <none or concise failure>
Changed scope: <files/modules>
Risk/assumption: <what remains uncertain>
Next slice: <one bounded behavior>
```

For long-running work, checkpoint at stable boundaries: after a successful focused check, before a risky migration or interface change, and after an artifact is produced. A checkpoint must be sufficient for another agent to resume without rereading the entire conversation.

## Review before completion

Inspect the final diff and its callers. Check that:

- the change maps to the objective and no unrelated cleanup slipped in;
- success, failure, empty, timeout, and boundary states have defined behavior;
- errors propagate at the correct layer and resources are released;
- configuration, generated artifacts, docs, and tests agree with the implementation;
- compatibility and security assumptions are explicit;
- logs/traces/metrics do not alter the behavior being measured.
