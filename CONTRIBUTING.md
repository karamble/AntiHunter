# Contributing to Halberd

Thank you for your interest in contributing to Halberd. **Use the provided templates [here](https://github.com/karamble/halberd/issues/new/choose) to get started.**

## Development Setup

### Prerequisites
- PlatformIO IDE or PlatformIO Core
- ESP32 toolchain
- Supported [hardware](https://github.com/karamble/halberd?tab=readme-ov-file#hardware-requirements) 

### Building from Source
```bash
git clone https://github.com/karamble/halberd.git
cd halberd
pio run
```

### Code Style

- Use consistent indentation (existing codebase style)
- Add comments for complex logic
- Follow existing naming conventions
- Test on actual hardware before submitting

### Submitting Changes

- Fork the repository
- Create a feature branch (git checkout -b feature/your-feature)
- Commit changes with clear messages
- Test thoroughly on hardware
- Push to your fork
- Open a Pull Request

### Pull Request Guidelines

- Describe what your PR does and why
- Reference related issues
- Include testing details and hardware used
- Ensure code compiles without warnings
- Keep PRs focused on single features/fixes

### Reporting Issues
- Use the bug report template
- Include serial output when relevant
- Specify hardware configuration
- Describe steps to reproduce

### Code of Conduct
- Be respectful, inclusive, and constructive

### Questions
Open a discussion on GitHub Discussions for questions.

## License boundary

Halberd is a hard fork of AntiHunter at commit `4680ea2` (tag `v-last-mit`),
the last commit before upstream relicensed to AGPL-3.0. Halberd continues
under the MIT License.

Do not cherry-pick, merge, or copy code from `lukeswitz/AntiHunter` past
that commit. Such commits are AGPL-licensed and incompatible with this
project. If a useful fix has landed upstream post-relicense, reimplement
it from the issue or bug description. Do not copy the patch.

Bug reports referencing upstream issues are welcome.
