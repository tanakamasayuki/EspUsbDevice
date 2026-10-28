# Archived 2.x assets

These files preserve the pre-v3 examples, tests and workflow configuration.
They are historical reference material, not runnable v3 examples or test gates.
Relative paths and dependency assumptions still describe their original layout;
use the corresponding 2.x git ref when running the old suite.

Use the repository's `validation/` environment for v3 hardware tests, and the
current `examples/` directory for v3 usage. No old conftest/helper is loaded by
that environment. Workflows stored here are inactive.

2.xのexamples/tests/workflowを保管する場所です。旧配置を前提とした相対path・依存は
書き換えず、実行する場合は対応する2.xのgit refを使います。v3のAPIや合否基準には使いません。
v3の試験は`validation/`、利用例は現在の`examples/`を参照してください。
