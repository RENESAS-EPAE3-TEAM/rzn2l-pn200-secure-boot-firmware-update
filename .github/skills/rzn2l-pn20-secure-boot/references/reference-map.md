# RZN2L PN2.0 Secure Boot Reference Map

This map points the skill to the maintained project documents. Prefer the active source, linker scripts, project settings, and generated artifacts whenever a document conflicts with the current workspace.

| Task | Read First | Use It For |
| --- | --- | --- |
| Loader + App architecture, memory layout, manifest, integrated versus standalone debug | [README_loader_app_split.md](../../../../README_loader_app_split.md) | Legacy RZAP manifest, fixed App partition, segment deployment, App LDR BTCM offset, App entry point, ICF/debug modes. |
| Routine Secure SSBL/App package generation and board verification | [pn20_rzn2l_secure_boot_python_validation_cn.md](../../../../pn20_rzn2l_secure_boot_python_validation_cn.md) | Tool inputs, padding, signed artifacts, xSPI write order, public-key hash check, UART acceptance criteria, and provisioning boundary. |
| Security architecture and secure firmware-update design | [pn20_rzn2l_secure_ssbl_fwupdate_proposal_cn.md](../../../../pn20_rzn2l_secure_ssbl_fwupdate_proposal_cn.md) | Authenticated scatter deployment, manifest validation, encryption direction, dual-bank switching, rollback control. |
| Current Scheme A/B decision | [pn20_rzn2l_secure_app_scheme_comparison_cn.md](../../../../pn20_rzn2l_secure_app_scheme_comparison_cn.md) | Why Scheme B signs the whole legacy App body and why verification precedes legacy RZAP parsing. |
| Secure SSBL FreeRTOS/startup troubleshooting | [pn20_secure_ssbl_freertos_fault_investigation_cn.md](../../../../pn20_secure_ssbl_freertos_fault_investigation_cn.md) | Startup fault analysis and initialization-order investigation. |

## Workspace Facts To Recheck

- The App and Loader/SSBL project names, active build configurations, artifact paths, and ICF selections can change. Inspect the current files before issuing commands or modifying code.
- IAR settings and debug files may retain source-machine absolute paths. Validate them after importing or changing configurations.
- RASC generation can rewrite managed IAR project groups. Keep custom RSIP sources and required include paths outside managed groups as documented by the project configuration.
