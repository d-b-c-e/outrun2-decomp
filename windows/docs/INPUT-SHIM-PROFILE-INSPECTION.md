# Optional profile action inspection

`dbce_shim_profile_action_info` is an additive optional export. Base ABI 2 does not
imply its presence; consumers must resolve it explicitly and explain a missing
matching package. No old export or ABI layout/version changes.

The caller provides exact UTF-8 profile bytes (maximum 64 KiB) and a published
action index. Core's `read_profile` parses the complete file. `plan_profile`, the
pure binding-admission routine also used by `Core::set_profile`, preserves the
same schema, invalid-action and instance/product-consistency rules.
Inspection uses the full action set and returns no identity if any action is
invalid or conflicting. Ordinary Core input still uses its valid subset as before.

In the toolkit, the export reads at controls schema 1, the schema the DLL's hosts
apply, so a Schema 2 profile reports `invalid` with the parser's reason. It passes
an empty `Capability`, because a pure call knows no adapter declaration, so every
action counts as supported. It therefore validates the profile's bindings; it
does not establish that a game adapter supports an action.

`Core::set_profile` runs once per Core: the device table is immutable after it.
Every caller follows that, including ProviderHost, the tests and the journal
fixture.

The call creates no host, worker, provider, device or force output; it does not
read or write files. It needs no open handle and may run on any thread. Results
follow the existing bounded UTF-8 text/needed convention. A caller must reject
truncation. JSON schema 1 contains `action`, `status`, `reason`, `sha256`, `kind`,
`instance`, `product` and `name`. Status is `bound`, `unbound` or `invalid`;
identity fields are empty unless status is bound. SHA-256 covers the exact full
profile bytes, including a BOM or other sections.

This is configured identity only. It does not qualify device presence, opened
handle identity, force capability, ranges or physical output. Art uses the
steering identity before its existing force owner acquires the device, then its
existing strict GUID/capability selection and reader qualification still apply.
It keeps explicit user overrides and never schedules a force reconnect from a
later shared-input attachment callback.

Targeted pure plan and ABI tests are added. In the toolkit they run in
`build_test.ps1`. The built-DLL export check retains all existing names and adds
the optional inspector. Existing Core, ABI and copied-provider suites must pass
before a consumer repins a native artifact.
