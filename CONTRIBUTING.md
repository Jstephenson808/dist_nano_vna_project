# How to Contribute

**If you find a bug**
- **Check Issues** [here](https://github.com/Jstephenson808/dist_nano_vna_project/issues): it may have already been reported!
- If it has not already been reported, **open a new issue**. Attach as much information as possible, and ideally test cases

**If you have made a fix**
- Always follow our style and conventions (see below)
- **Make a Pull Request**, clearly explaining your solution and linking to the relevant issue if applicable. Follow the guidelines below to make sure you don't miss anything!

**If you wish to make another change**
- This includes new features and general improvements.
- Always follow our style and conventions (see below)
- **Make an Issue** detailling the proposed changes and the rationale behind them. Try to keep the scope of these changes localised, issues that require major changes create large pull requests which create problems. Iterative changes are better than major reworks.
- **Make a Pull Request** Linking to the issue and providing relevant tests and documentation. Follow the guidelines below to make sure you don't miss anything!

## Code Guidelines

**Style**

For C code, we use the [Linux kernel coding style](https://www.kernel.org/doc/html/v4.10/process/coding-style.html). Try to keep functions clear and concise, and without unreasonable levels of nesting. 
Our C source code is split into distinct units -- their purposes are clear, and we encourage you to keep them as loosely coupled as possible. Each file should handle its own responsibilities independently, and their public interfaces should be clear and easy to use.

For Python code, 

**Pull Request Style**

As a rule, all pull requests should have narrow scope, affecting as few files as possible. They should pass all unit tests, and introduce tests of their own for functions or behaviour that they add. Changes without tests will be rejected. Pull requests should also update documentation where relevant -- if installation or testing workflows change look at the [readme](README.md), if user-facing behaviour changes look at the [user guide](USERGUIDE.md).

Additionally, Pull Requests should detail the following:
- What does this PR do and why?
- Changelog for user-facing changes
- How to set up and validate locally
- Relevant tests

Every pull requests will be reviewed before being merged, and these reviews should ensure that:
- Code functions as advertised
- Style guides are followed
- Automated tests have been created and pass
- Documentation has been updated
- All user-facing changes are documented

**Non-configuration Files**

Please do not commit compiled code, output files, etc.