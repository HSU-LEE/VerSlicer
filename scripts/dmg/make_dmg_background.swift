// Draws the VerSlicer DMG window background (drag-to-install arrow and the one
// Terminal line for a blocked first launch) at 1x and 2x.
//
// Usage: swift scripts/dmg/make_dmg_background.swift <out_dir>
// Writes background.png and background@2x.png. The window layout in
// package_macos_dmg.sh must match the positions below.

import AppKit

let width: CGFloat = 660
let height: CGFloat = 440

// Icon centres (points, top-left origin). Keep in sync with package_macos_dmg.sh.
let appCenter = CGPoint(x: 180, y: 190)
let applicationsCenter = CGPoint(x: 480, y: 190)

let top = NSColor(srgbRed: 1.0, green: 0.976, blue: 0.957, alpha: 1)
let bottom = NSColor(srgbRed: 0.957, green: 0.937, blue: 0.914, alpha: 1)
let brand = NSColor(srgbRed: 0.937, green: 0.490, blue: 0.196, alpha: 1)
let ink = NSColor(srgbRed: 0.20, green: 0.19, blue: 0.18, alpha: 1)
let muted = NSColor(srgbRed: 0.50, green: 0.48, blue: 0.46, alpha: 1)

func text(_ s: String, y: CGFloat, font: NSFont, color: NSColor, kern: CGFloat = 0) {
    let centered = NSMutableParagraphStyle()
    centered.alignment = .center
    NSAttributedString(string: s, attributes: [.font: font, .foregroundColor: color, .paragraphStyle: centered,
                                               .kern: kern])
        .draw(with: NSRect(x: 0, y: y, width: width, height: 30), options: [.usesLineFragmentOrigin])
}

func render(scale: CGFloat) -> Data {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(width * scale), pixelsHigh: Int(height * scale),
                               bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    rep.size = NSSize(width: width, height: height)
    NSGraphicsContext.saveGraphicsState()
    let base = NSGraphicsContext(bitmapImageRep: rep)!
    // Flip so y grows downward, like Finder window coordinates.
    base.cgContext.translateBy(x: 0, y: height)
    base.cgContext.scaleBy(x: 1, y: -1)
    NSGraphicsContext.current = NSGraphicsContext(cgContext: base.cgContext, flipped: true)

    // Warm vertical gradient with a faint brand glow at the top.
    NSGradient(starting: top, ending: bottom)!.draw(in: NSRect(x: 0, y: 0, width: width, height: height), angle: 90)
    NSGradient(colors: [brand.withAlphaComponent(0.10), brand.withAlphaComponent(0)])!
        .draw(fromCenter: CGPoint(x: width / 2, y: -40), radius: 0, toCenter: CGPoint(x: width / 2, y: -40),
              radius: 360, options: [])

    // Title.
    text("VerSlicer", y: 34, font: NSFont.systemFont(ofSize: 24, weight: .bold), color: ink, kern: -0.3)
    text("Drag VerSlicer to Applications to install", y: 68,
         font: NSFont.systemFont(ofSize: 12.5), color: muted)

    // Solid arrow between the icons, level with their centres.
    let y = appCenter.y
    let x0 = appCenter.x + 84, x1 = applicationsCenter.x - 84
    let arrow = NSBezierPath()
    arrow.move(to: CGPoint(x: x0, y: y))
    arrow.line(to: CGPoint(x: x1, y: y))
    arrow.move(to: CGPoint(x: x1 - 16, y: y - 16))
    arrow.line(to: CGPoint(x: x1, y: y))
    arrow.line(to: CGPoint(x: x1 - 16, y: y + 16))
    arrow.lineWidth = 6
    arrow.lineCapStyle = .round
    arrow.lineJoinStyle = .round
    brand.setStroke()
    arrow.stroke()

    // If macOS refuses the unsigned app, this one Terminal line fixes it.
    text("Won’t open? Run this once in Terminal:", y: 336,
         font: NSFont.systemFont(ofSize: 11.5, weight: .medium), color: muted)
    let cmd = "xattr -dr com.apple.quarantine /Applications/VerSlicer.app"
    let mono = NSFont.monospacedSystemFont(ofSize: 11.5, weight: .regular)
    let w = (cmd as NSString).size(withAttributes: [.font: mono]).width + 32
    let pill = NSRect(x: (width - w) / 2, y: 358, width: w, height: 28)
    NSColor.white.withAlphaComponent(0.9).setFill()
    NSBezierPath(roundedRect: pill, xRadius: 14, yRadius: 14).fill()
    NSColor.black.withAlphaComponent(0.08).setStroke()
    let border = NSBezierPath(roundedRect: pill.insetBy(dx: 0.5, dy: 0.5), xRadius: 13.5, yRadius: 13.5)
    border.lineWidth = 1
    border.stroke()
    text(cmd, y: 364, font: mono, color: ink)

    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

let outDir = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "."
try render(scale: 1).write(to: URL(fileURLWithPath: outDir).appendingPathComponent("background.png"))
try render(scale: 2).write(to: URL(fileURLWithPath: outDir).appendingPathComponent("background@2x.png"))
print("wrote \(outDir)/background.png and background@2x.png")
