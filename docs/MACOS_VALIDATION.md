# macOS 実機検証チェックリスト

## ビルド
- Xcode / macOS / AE / SDK の正確なバージョン、機種・CPU・GPU・メモリを記録。
- READMEの手順でクリーンビルド。Apple Siliconはarm64ネイティブ、Intelは対応AEがある場合のみx86_64。
- SDK不要CPUテスト全件、Metal smoke test（SKIPではないこと）、PiPL・エクスポート・署名チェック。
- ビルド失敗時は最初のコンパイラエラーと全ログを共有。SDKやAE本体をアップロードしない。

## AEホスト（手動）
1. AEを終了して、完成した Gradation.plugin を手動でPlug-insへ置く。AEを自分で起動する。
2. Generate > Gradation を適用。8/16/32bpc、Linear/Radial/Angle/Mapping、全色空間、Blend、Alpha、Loop/Reflect/Invert、HDR・透明画素を確認。
3. 32bpcで Render Device をCPU/Auto/GPUに切替、CPUとMetalの画素差・描画時間・フレームの異常を確認。AE側でMetalを選択できない場合はCPUフォールバックを確認する。
4. 10インスタンス、MFR、長時間プレビュー、ROI・クロップ・奇数幅・キーフレーム、プロジェクト再読込、GPUデバイス再設定を試す。
5. Gradient Custom UIとカラーピッカーを確認。H/S/B/R/G/B/A軸、Hex、8/16/32bpc数値入力、OK/Cancel、連続開閉、Retina、ダーク/ライトテーマ。Macは現状AppKit独自描画で、WindowsのFigma原本との完全な見た目の一致は未検証。ライブAEプレビューは安全性優先で無効。
6. CPU/GPU負荷、待機時、カラーピッカー起動、1080p/4Kでのp50/p95を計測。1ms/待機1%目標を結果と区別し、未測定項目は未測定と記載する。
7. SDKのMetal GPU世界は32bpc。8/16bpcはCPUで動かす。GRADATION_DISABLE_METAL=1 は明示的な診断用CPUフォールバック。

## 共有してほしい情報
- ビルドのcommit SHA、macOS、Xcode、AE/SDKのバージョン、arm64/x86_64。
- CTestログ、Metal parity結果、AEのCPU/GPU画像比較、FHDの時間、MFRと待機時の測定。
- クラッシュがあれば該当操作とクラッシュレポート（個人情報やプロジェクト情報は除去）。
