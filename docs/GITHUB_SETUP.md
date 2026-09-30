# GitHub Repository Setup

## 1. Create the GitHub repository

On GitHub, create a new repository named:

```text
Smart-Wireless-Touchpad-Air-Mouse
```

Do not add a second README, `.gitignore`, or license if you are going to push this repository as-is.

## 2. Configure Git identity

Run once on the development computer:

```bash
git config --global user.name "Your Name"
git config --global user.email "you@example.com"
```

Or configure identity only for this project:

```bash
git config user.name "Your Name"
git config user.email "you@example.com"
```

## 3. Initialize and commit

From the repository root:

```bash
git init
git add .
git commit -m "docs: initial project repository"
git branch -M main
```

## 4. Connect GitHub

Copy the HTTPS repository URL from GitHub and run:

```bash
git remote add origin https://github.com/<YOUR-ACCOUNT>/Smart-Wireless-Touchpad-Air-Mouse.git
git remote -v
git push -u origin main
```

For SSH instead:

```bash
git remote add origin git@github.com:<YOUR-ACCOUNT>/Smart-Wireless-Touchpad-Air-Mouse.git
git push -u origin main
```

## 5. Normal team workflow

```bash
git pull origin main
git checkout -b feature/my-change
# edit files
git add .
git commit -m "feat: describe the change"
git push -u origin feature/my-change
```

Then open a pull request into `main`.

## 6. Before merging hardware changes

Require the pull request to include:

- code change;
- updated pin map if wiring changed;
- updated test checklist;
- updated protocol documentation if the packet changed;
- board/core/library version information if build behavior changed.

## 7. Recommended GitHub repository description

```text
Dual-ESP32 wireless touchpad and air mouse for Windows PC using an ESP32-WROOM-32 handheld transmitter and ESP32-S3 native USB HID receiver.
```
