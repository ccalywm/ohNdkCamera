import testNapi from 'libentry.so';
import { abilityAccessCtrl, common } from '@kit.AbilityKit';
import { image } from '@kit.ImageKit';

@Entry
@Component
struct Index {
  @State mSurfaceId: string = '';
  @State pixmap: PixelMap | undefined = undefined;
  xComponentController: XComponentController = new XComponentController()
  private surfaceId: string = '';
  private mReceiver: image.ImageReceiver = this.createImageReceiver();

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
    //申请权限
    let context = getContext() as common.UIAbilityContext;
    abilityAccessCtrl.createAtManager().requestPermissionsFromUser(context, ['ohos.permission.CAMERA']).then(() => {
      this.surfaceId = this.xComponentController.getXComponentSurfaceId();

      console.log("申请权限 我是surfaceID：",this.surfaceId);

      // testNapi.initCamera(this.surfaceId, 2, 0)
    });
    console.info(`surfaceId=${this.surfaceId}`);
  }

  build() {
    Row() {
      Column() {
        XComponent({
          id: 'xcomponent',
          type: XComponentType.SURFACE,
          controller: this.xComponentController
        })
          .onLoad(async () => {
            this.surfaceId = this.xComponentController.getXComponentSurfaceId();
            console.log("我是surfaceID：",this.surfaceId);
            testNapi.initCamera(this.surfaceId, 2, 0)


          })
          .height('80%').width("60%")
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



