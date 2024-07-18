//
//  MetalView.swift
//  pandios
//
//  Created by m1 on 18/07/2024.
//

import SwiftUI
import MetalKit

struct MetalView: UIViewRepresentable {
    class Coordinator: NSObject, MTKViewDelegate {
        var parent: MetalView
        var device: MTLDevice!
        var commandQueue: MTLCommandQueue!
        
        init(parent: MetalView) {
            self.parent = parent
            super.init()
            device = MTLCreateSystemDefaultDevice()
            commandQueue = device.makeCommandQueue()
        }
        
        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
            // Handle size change if needed
        }
        
        func draw(in view: MTKView) {
            // Draw stuff
        }
    }
    
    func makeCoordinator() -> Coordinator {
        Coordinator(parent: self)
    }

    func makeUIView(context: Context) -> MTKView {
        let mtkView = MTKView()
        mtkView.device = context.coordinator.device;
        mtkView.delegate = context.coordinator
        mtkView.preferredFramesPerSecond = 60
        mtkView.enableSetNeedsDisplay = true;
        return mtkView
    }
    
    func updateUIView(_ uiView: MTKView, context: Context) {

    }
}
