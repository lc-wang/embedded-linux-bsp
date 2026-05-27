# Sashiko 本機 Review 架設與復現紀錄

本文記錄如何在本機架設並執行 `sashiko`，用來重跑 Linux kernel patch 的 AI review。

主要目標是：在重新送 upstream patch 之前，可以先在 localhost 針對同一個 kernel commit 跑一次 Sashiko review，確認原本被指出的問題是否已經修正。

## 1. 背景

在將 Linux kernel patch 送到 upstream 後，收到 Sashiko 產生的 review 回覆。為了確認修正後是否還會被指出同樣問題，因此嘗試在本機復現 Sashiko review 流程。

本次測試的 patch subject：

```text
drm/tiny: add support for PIXPAPER 4.26 monochrome e-ink panel
```

本機 kernel branch：

```bash
git branch
# * b4/bar
#   master
```

## 2. 測試環境

本次使用的路徑範例：

```text
Sashiko source:
  /media/lcwang/76623810-90ce-4337-bfcc-1ab3b859a0e41/sashiko

Linux kernel tree:
  /media/lcwang/76623810-90ce-4337-bfcc-1ab3b859a0e41/linux_kernel_lc/linux
```

Rust / Cargo 版本需要夠新。若 Cargo 太舊，可能會出現：

```text
feature `edition2024` is required
```

如果系統上的 Cargo 版本太舊，建議透過 `rustup` 安裝與更新 Rust：

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"

rustup update stable
rustup default stable

cargo --version
rustc --version
```

確認目前使用的是 rustup 提供的 Cargo：

```bash
which cargo
# /home/<user>/.cargo/bin/cargo
```

不要優先使用 Ubuntu apt 內建的 `cargo` / `rustc`，因為版本可能太舊。

## 3. 從 source build Sashiko

Clone 並 build Sashiko：

```bash
git clone --recursive https://github.com/sashiko-dev/sashiko.git
cd sashiko

cargo build --release --bins
```

build 完後應該可以在 `target/release/` 看到相關 binary：

```bash
ls -l target/release/
```

常用 binary：

```text
sashiko
sashiko-cli
review
```

先確認 `review` binary 可以正常啟動：

```bash
RUST_BACKTRACE=full RUST_LOG=debug ./target/release/review --help
```

若這個指令可以正常印出 help，表示 `review` binary 本身可啟動。

## 4. 初始問題：`sashiko-cli local` exit code 101

一開始在 kernel tree 執行：

```bash
cd /path/to/linux

/path/to/sashiko/target/release/sashiko-cli local --force-local HEAD
```

遇到錯誤：

```text
[1/4] Extracting patches from HEAD... (1 commit)
[2/4] Starting review subprocess...
Error: Review subprocess produced no output (exit code: 101)
```

這個錯誤不是 kernel patch 本身造成的，而是 `review` subprocess 發生 panic，但 stderr 預設沒有被完整印出來。

為了 debug，暫時修改 `src/bin/sashiko-cli.rs`，將 review subprocess 的 stderr 全部印出。

原本程式碼附近：

```rust
while let Ok(Some(line)) = lines.next_line().await {
    if !saw_applying && line.contains("Applying") {
```

暫時改成：

```rust
while let Ok(Some(line)) = lines.next_line().await {
    eprintln!("[review stderr] {}", line);

    if !saw_applying && line.contains("Applying") {
```

重新 build：

```bash
cd /path/to/sashiko
cargo build --release --bins
```

再重新執行：

```bash
cd /path/to/linux

RUST_BACKTRACE=full RUST_LOG=debug \
/path/to/sashiko/target/release/sashiko-cli \
  local \
  --force-local \
  HEAD \
  2>&1 | tee /tmp/sashiko-local-debug.log
```

## 5. Settings.toml 缺少必要欄位

打開 stderr 後，第一個真正的錯誤是：

```text
Failed to load settings: missing configuration field "database"
```

補上 `[database]` 後，又依序出現：

```text
missing configuration field "nntp"
missing configuration field "server"
```

這代表 local `Settings.toml` 不能只有 AI provider 設定，還需要包含 Sashiko 預期的完整 section。

從 `src/settings.rs` 可看到 `database.url` 與 `database.token` 都是必填欄位，此外頂層還需要 `nntp`、`mailing_lists`、`ai`、`server`、`git`、`review` 等設定。

## 6. 最小可用 Settings.toml

在 Linux kernel tree 底下建立本機用的 `Settings.toml`：

```bash
cd /path/to/linux

cat > Settings.toml <<'EOF_CFG'
log_level = "info"

[database]
url = "sqlite:///tmp/sashiko.db"
token = ""

[nntp]
server = "nntp.lore.kernel.org"
port = 563

[mailing_lists]
track = ["linux-kernel@vger.kernel.org"]

[ai]
provider = "copilot-cli"
model = "gpt-5-mini"

[server]
host = "127.0.0.1"
port = 8080
read_only = false

[git]
repository_path = "/path/to/linux"

[review]
concurrency = 1
worktree_dir = "/tmp/sashiko-worktrees"
timeout_seconds = 600
max_retries = 0
max_lines_changed = 2000
max_files_touched = 100
ignore_files = []
email_policy_path = ""

max_total_tokens = 150000
max_total_output_tokens = 16000
EOF_CFG
```

請將：

```text
/path/to/linux
```

替換成實際 Linux kernel tree 路徑。

建立 worktree 目錄：

```bash
mkdir -p /tmp/sashiko-worktrees
```

這份 `Settings.toml` 只是本機測試用，不要 commit 到 kernel tree：

```bash
echo Settings.toml >> .git/info/exclude
```

## 7. 先用 `--no-ai` 驗證 patch application

在真正跑 AI review 前，先用 `--no-ai` 確認本機流程可以跑通：

```bash
cd /path/to/linux

RUST_BACKTRACE=full RUST_LOG=debug \
/path/to/sashiko/target/release/sashiko-cli \
  local \
  --force-local \
  --no-ai \
  HEAD \
  2>&1 | tee /tmp/sashiko-local-no-ai.log
```

成功時應該看到：

```text
Patch Application:
  [1] applied (checkout)
```

成功輸出範例：

```text
[1/4] Extracting patches from HEAD... (1 commit)
[2/4] Starting review subprocess...
[review stderr] Loaded patchset via JSON: drm/tiny: add support for PIXPAPER 4.26 monochrome e-ink panel (ID: 0)
[review stderr] Resolving baseline: <commit>^
[review stderr] Creating worktree at "/tmp/..."
[review stderr] Applying all 1 patches to validate series...
[review stderr] Patch 1 is identified by commit ID <commit>, attempting direct checkout...
[review stderr] Skipping AI review due to --no-ai flag.

Local Review Results
  Input:    HEAD
  Baseline: <commit>^

Patch Application:
  [1] applied (checkout)
```

這代表：

```text
Settings.toml 可以被載入
baseline 可以解析
temporary worktree 可以建立
patch 可以 apply / checkout
review subprocess 可以正常執行
```

## 8. 使用 Copilot CLI 作為 AI provider

本次環境只有 Copilot API / Copilot CLI 可用，因此 Sashiko 的 AI provider 設為：

```toml
[ai]
provider = "copilot-cli"
model = "gpt-5-mini"
```

`copilot-cli` provider 會呼叫本機的 `copilot` command。

先確認 Copilot CLI 存在：

```bash
which copilot
copilot --help
```

直接測試 Copilot CLI：

```bash
echo 'Return only JSON: {"concerns":[]}' | \
copilot --model gpt-5-mini --output-format json -s --no-custom-instructions
```

預期 assistant message 中會包含：

```json
{"concerns":[]}
```

如果 Copilot 尚未認證，可能會出現：

```text
Error: No authentication information found.
```

可以使用以下其中一種方式認證：

```bash
copilot
# 進入後執行 /login
```

或：

```bash
gh auth login
```

或設定 token：

```bash
export COPILOT_GITHUB_TOKEN="..."
export GH_TOKEN="..."
export GITHUB_TOKEN="..."
```

## 9. 修正 prompt path 問題

從 Linux kernel tree 執行時，Sashiko 可能會用目前工作目錄尋找 prompt：

```text
third_party/prompts/kernel/subsystem/subsystem.md
```

如果找不到，log 可能出現：

```text
subsystem.md not found for Phase 0 at "third_party/prompts/kernel/subsystem/subsystem.md"
```

可以在 kernel tree 建立 symlink，指向 Sashiko source 內的 prompts：

```bash
cd /path/to/linux

mkdir -p third_party/prompts
ln -sfn /path/to/sashiko/third_party/prompts/kernel \
  third_party/prompts/kernel
```

修正後，Phase 0 可以正常選擇 subsystem guide。

成功範例：

```text
Executing Phase 0: Pre-screening relevant subsystem guides.
Phase 0 selected prompts: ["drm.md", "of.md", "kconfig.md", "dt-bindings.md"]
```

## 10. 執行完整 local AI review

執行：

```bash
cd /path/to/linux

RUST_BACKTRACE=full RUST_LOG=debug \
/path/to/sashiko/target/release/sashiko-cli \
  local \
  --force-local \
  HEAD \
  2>&1 | tee /tmp/sashiko-copilot-review.log
```

成功時會看到：

```text
[4/4] Review complete.
```

本次 Pixpaper DRM patch 的成功結果範例：

```text
Patch Application:
  [1] applied (checkout)

Review Findings:
Critical: 0 · High: 2 · Medium: 2 · Low: 1
```

## 11. Pixpaper DRM patch 的 review 結果摘要

本次 Sashiko local review 對 Pixpaper DRM driver 提出幾類問題。

### 11.1 同步 SPI/GPIO 更新流程

Sashiko 對 plane update path 中的同步操作提出疑問，包括：

```text
pixpaper_wait_busy()
usleep_range()
gpiod_get_value_cansleep()
spi_write()
kzalloc(GFP_KERNEL)
```

它質疑這些操作是否應該直接在 DRM atomic update path 中執行，或是否應該改成 worker / deferred update。

注意：DRM 的 “atomic” 不一定等同 Linux kernel hard atomic context。因此這個 finding 需要謹慎判斷，不應直接解讀為「一定不能 sleep」。比較實際的問題是：

```text
commit/update path 是否可能 block 太久
電子紙更新時間較長時是否會影響 commit latency
是否需要 worker 或 deferred update path
```

### 11.2 Busy timeout 沒有往上回傳錯誤

目前 busy wait timeout 只 log warning：

```c
drm_warn(&panel->drm, "Busy wait timed out\n");
return;
```

建議修正方向：

```text
透過 error context 回傳 -ETIMEDOUT，讓 caller 可以正確處理 timeout。
```

### 11.3 Buffer allocation size overflow

Sashiko 指出 destination buffer size 使用：

```c
dst_pitch * fb->height
```

但沒有明確 overflow check。

建議修正方向：

```text
使用 size_t 搭配 check_mul_overflow() 後再 allocation。
```

### 11.4 Framebuffer pitch 與 unaligned access

轉換程式碼直接將 framebuffer row cast 成 `u32 *`：

```c
const uint32_t *src_pixels = (const uint32_t *)src_row;
pixel = src_pixels[src_x];
```

可能問題：

```text
src_pitch 可能小於 width * 4
src_row 可能沒有 32-bit alignment
某些架構可能因 unaligned 32-bit access 造成 fault
```

建議修正方向：

```text
檢查 pitch，並使用 get_unaligned() 或 byte-wise access。
```

### 11.5 Remove path cleanup

Sashiko 也詢問 remove path 是否完整釋放 probe 中註冊的資源，例如：

```text
drm_dev_register()
drm_client_setup()
```

建議後續確認：

```text
確認 drm_dev_unregister()、drm_atomic_helper_shutdown()、client cleanup 等生命週期是否正確。
```

## 12. 建議修正優先順序

對 Pixpaper DRM driver 來說，較安全的短期修正順序：

```text
1. busy timeout 回傳 -ETIMEDOUT。
2. 加上 framebuffer pitch validation。
3. 加上 dst buffer allocation overflow check。
4. 避免直接做 unaligned u32 framebuffer read。
5. 重新評估同步 SPI update 是否需要 deferred / worker 化。
```

不建議一開始就大改成 worker 架構，除非 reviewer 明確要求，或已確認 commit/update path blocking 是實際問題。

## 13. 常用指令整理

Build Sashiko：

```bash
cd /path/to/sashiko
cargo build --release --bins
```

驗證 patch application，不跑 AI：

```bash
cd /path/to/linux

/path/to/sashiko/target/release/sashiko-cli \
  local \
  --force-local \
  --no-ai \
  HEAD
```

執行完整 local AI review：

```bash
cd /path/to/linux

RUST_BACKTRACE=full RUST_LOG=debug \
/path/to/sashiko/target/release/sashiko-cli \
  local \
  --force-local \
  HEAD \
  2>&1 | tee /tmp/sashiko-review.log
```

測試 Copilot CLI：

```bash
echo 'Return only JSON: {"concerns":[]}' | \
copilot --model gpt-5-mini --output-format json -s --no-custom-instructions
```

避免將 local 設定 commit 進 repo：

```bash
echo Settings.toml >> .git/info/exclude
```

## 14. 注意事項

本機復現結果不一定會和 upstream Sashiko 回覆完全一樣。

可能差異來源：

```text
Sashiko 版本
AI provider / model
prompt 版本
kernel base commit
mailing-list thread context
model output randomness
```

local reproduction 的目標不是得到一模一樣的文字，而是確認：

```text
修正後，原本被指出的同類問題是否還會出現。
```
