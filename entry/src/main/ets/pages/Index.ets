import testNapi from 'libentry.so';
import { abilityAccessCtrl, common } from '@kit.AbilityKit';
import { image } from '@kit.ImageKit';

@Entry
@Component
struct Index {
  @State mSurfaceId: string = '';
  @State pixmap: PixelMap | undefined = undefined;
  xComponentController: XComponentController = new XComponentController()
  xComponentController1: XComponentController = new XComponentController()
  private surfaceId: string = '';
  private mReceiver: image.ImageReceiver = this.createImageReceiver();

  // 控制变量
  private cameraInitialized: boolean = false;
  private decoderSurfaceId: string = '';

  createImageReceiver(): image.ImageReceiver {
    let size: image.Size = {
      height: 480,
      width: 480
    }
    let receiver: image.ImageReceiver = image.createImageReceiver(size, image.ImageFormat.JPEG, 8);
    console.info('imageArrival11');
    receiver.on("imageArrival", async (e, data) => {
      console.info('imageArrival');
      let imageData = await receiver.readNextImage()
      let imgComponent = await imageData.getComponent(image.ComponentType.JPEG)
      image.createImageSource(imgComponent.byteBuffer)
        .createPixelMap().then((px) => {
        this.pixmap = px
      })
      imageData.release();
    })
    return receiver;
  }

  async getPhotoSurfaceID() {
    if (this.mReceiver) {
    } else {
      this.mReceiver = this.createImageReceiver();
    }
    if (this.mReceiver) {
      this.mSurfaceId = await this.mReceiver.getReceivingSurfaceId();
    }
  }

  async isVideoPhotoFn() {
    await this.getPhotoSurfaceID();
    testNapi.startPhotoOrVideo('photo', '0', this.mSurfaceId);
  }

  async aboutToAppear() {
    // 申请权限
    let context = getContext() as common.UIAbilityContext;
    abilityAccessCtrl.createAtManager().requestPermissionsFromUser(context, ['ohos.permission.CAMERA']).then(() => {
      console.log("权限申请完成");
    });
    // 注意：移除了 initCamera 调用，现在由 XComponent onLoad 触发
  }

  // 辅助方法：启动编解码（如果条件满足）
  private startEncoderIfReady() {
    if (this.cameraInitialized && this.decoderSurfaceId) {
      console.log("两个 XComponent 都已加载，启动编解码，解码ID:", this.decoderSurfaceId);
      testNapi.startEncodeDecode(this.decoderSurfaceId);
    } else {
      console.log("编解码条件未满足，相机初始化:", this.cameraInitialized, "解码ID:", this.decoderSurfaceId);
    }
  }

  build() {
    Row() {
      Column() {
        Row(){
          // 显示 XComponent（主预览）
          XComponent({
            id: 'xcomponent',
            type: XComponentType.SURFACE,
            controller: this.xComponentController
          })
            .onLoad(async () => {
              const id = this.xComponentController.getXComponentSurfaceId();
              this.surfaceId = id;
              console.log("显示XComponent加载，surfaceID:", id);
              // 初始化相机（同步调用，会阻塞直到完成）
              testNapi.initCamera(id, 2, 0);
              this.cameraInitialized = true;
              console.log("相机初始化完成");
              // 尝试启动编解码
              this.startEncoderIfReady();
            })
            .height('100%').width("50%")

          // 解码输出 XComponent
          XComponent({
            id: 'xcomponent',
            type: XComponentType.SURFACE,
            controller: this.xComponentController1
          })
            .onLoad(async () => {
              const id = this.xComponentController1.getXComponentSurfaceId();
              this.decoderSurfaceId = id;
              console.log("解码XComponent加载，解码surfaceID:", id);
              // 尝试启动编解码
              this.startEncoderIfReady();
            })
            .height('100%').width("50%")
        }
        .height('95%').width("100%")

        Button() {
          Text("拍照")
            .fontColor(Color.Black)
            .alignSelf(ItemAlign.Center)
            .onClick(() => {
              this.isVideoPhotoFn()
            })
        }
        .width('10%')
        .height(50)
        Image(this.pixmap)
          .width(200)
          .height(200)
      }
      .width('100%')
    }
    .height('100%')
  }
}

// import testNapi from 'libentry.so';
// import { abilityAccessCtrl, common } from '@kit.AbilityKit';
// import { image } from '@kit.ImageKit';
//
// @Entry
// @Component
// struct Index {
//   @State mSurfaceId: string = '';
//   @State pixmap: PixelMap | undefined = undefined;
//   xComponentController: XComponentController = new XComponentController()
//   xComponentController1: XComponentController = new XComponentController()
//   private surfaceId: string = '';
//   private surfaceIdDe: string = '';
//   private mReceiver: image.ImageReceiver = this.createImageReceiver();
//
//   createImageReceiver(): image.ImageReceiver {
//     let size: image.Size = {
//       height: 480,
//       width: 480
//     }
//     let receiver: image.ImageReceiver = image.createImageReceiver(size, image.ImageFormat.JPEG, 8);
//     console.info('imageArrival11');
//     receiver.on("imageArrival", async (e, data) => {
//       console.info('imageArrival');
//       let imageData = await receiver.readNextImage()
//       let imgComponent = await imageData.getComponent(image.ComponentType.JPEG)
//       image.createImageSource(imgComponent.byteBuffer)
//         .createPixelMap().then((px) => {
//         this.pixmap = px
//       })
//       imageData.release();
//     })
//     return receiver;
//   }
//
//   async getPhotoSurfaceID() {
//     if (this.mReceiver) {
//     } else {
//       this.mReceiver = this.createImageReceiver();
//     }
//     if (this.mReceiver) {
//       this.mSurfaceId = await this.mReceiver.getReceivingSurfaceId();
//     }
//   }
//
//   async isVideoPhotoFn() {
//     await this.getPhotoSurfaceID();
//     testNapi.startPhotoOrVideo('photo', '0', this.mSurfaceId);
//   }
//
//   async aboutToAppear() {
//     //申请权限
//     let context = getContext() as common.UIAbilityContext;
//     abilityAccessCtrl.createAtManager().requestPermissionsFromUser(context, ['ohos.permission.CAMERA']).then(() => {
//       this.surfaceId = this.xComponentController.getXComponentSurfaceId();
//
//       console.log("申请权限 我是surfaceID：",this.surfaceId);
//
//       // testNapi.initCamera(this.surfaceId, 2, 0)
//     });
//     console.info(`surfaceId=${this.surfaceId}`);
//   }
//
//   build() {
//     Row() {
//       Column() {
//         Row(){
//           XComponent({
//             id: 'xcomponent',
//             type: XComponentType.SURFACE,
//             controller: this.xComponentController
//           })
//             .onLoad(async () => {
//               this.surfaceId = this.xComponentController.getXComponentSurfaceId();
//               console.log("我是surfaceID：",this.surfaceId);
//               testNapi.initCamera(this.surfaceId, 2, 0)
//               if (this.surfaceIdDe) {
//                 console.log("我是surfaceID 解码id有效：",this.surfaceIdDe);
//
//                 testNapi.startEncodeDecode(this.surfaceIdDe)
//               }
//
//             })
//             .height('100%').width("50%")
//
//           XComponent({
//             id: 'xcomponent',
//             type: XComponentType.SURFACE,
//             controller: this.xComponentController1
//           })
//             .onLoad(async () => {
//               this.surfaceId = this.xComponentController1.getXComponentSurfaceId();
//               this.surfaceIdDe = this.surfaceId;
//               console.log("surfaceIdDe：",this.surfaceId);
//
//             })
//             // .height('90%').width("70%")
//             .height('100%').width("50%")
//
//         } .height('95%').width("100%")
//
//
//         Button() {
//           Text("拍照")
//             .fontColor(Color.Black)
//             .alignSelf(ItemAlign.Center)
//             .onClick(() => {
//               this.isVideoPhotoFn()
//             })
//         }
//         .width('10%')
//         .height(50)
//         Image(this.pixmap)
//           .width(200)
//           .height(200)
//       }
//       .width('100%')
//     }
//     .height('100%')
//   }
// }
//
//
//
