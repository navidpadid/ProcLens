# Agentic Workflow Guide

## Purpose (High-Level)

ProcLens uses an agentic GitHub Actions workflow to keep documentation aligned with implementation changes.

The workflow:
- Monitors repository changes when pull requests are closed into main
- Maps code/process changes to the correct docs files
- Opens a pull request with documentation updates when needed
- Skips self-trigger loops from github-actions[bot] and Copilot actors

This is intentionally high-level. Detailed behavior and policy live in the workflow source file:
- `.github/workflows/update-docs-agent.md`

## Source of Truth

Use this model:
- Author/edit workflow logic in `.github/workflows/update-docs-agent.md`
- Treat `.github/workflows/update-docs-agent.lock.yml` as generated output

Do not hand-edit the lock file unless absolutely necessary for emergency triage.

## How To Update The Workflow

1. Edit the source workflow:

   `.github/workflows/update-docs-agent.md`

2. Apply your policy/trigger/tooling changes (for example: triggers, safe outputs, PR behavior, guard conditions).

3. Compile the workflow to regenerate the lock file.

## How To Compile

From repository root:

```bash
gh aw compile update-docs-agent
```

This updates:
- `.github/workflows/update-docs-agent.lock.yml`

Optional compile checks:

```bash
gh aw compile update-docs-agent --validate
gh aw compile update-docs-agent --actionlint
```

## Testing and Verification

### 1) Local Static Verification

Run compile and inspect the generated lock file for expected trigger/guard/output behavior.

Useful checks:

```bash
gh aw compile update-docs-agent
git diff -- .github/workflows/update-docs-agent.md .github/workflows/update-docs-agent.lock.yml
```

### 2) Trigger Verification (GitHub)

Validate expected event behavior in Actions:
- Automatic run on pull_request closed targeting main
- No automatic run on push
- No run when actor is github-actions[bot] or Copilot

A practical test sequence:
1. Open a test PR against main
2. Merge/close it
3. Confirm workflow execution in Actions
4. Confirm resulting documentation PR behavior and labels

### 3) Safe Output Verification

Ensure workflow output policy is still enforced:
- PR creation path is allowed
- Protected files/path prefixes remain respected
- Unexpected writes fall back to issue/no-op handling as configured

## Maintenance Notes

- Keep workflow description aligned with real trigger behavior.
- When changing workflow behavior, update this document if operational steps change.
- Prefer small policy changes and recompile immediately to avoid source/generated drift.
