package fun.dragonfly.switchfin;

import android.database.ContentObserver;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.os.Handler;
import android.provider.Settings;
import android.view.Surface;
import android.view.SurfaceView;

import org.libsdl.app.BorealisHandler;
import org.libsdl.app.PlatformUtils;
import org.libsdl.app.SDLActivity;

public class SwitchfinActivity extends SDLActivity
{
    protected static SurfaceView mpvSurface;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mSurface.getHolder().setFormat(PixelFormat.RGBA_8888);
        mSurface.setZOrderOnTop(true);
        mpvSurface = new SurfaceView(this);
        mLayout.addView(mpvSurface, 0);

        PlatformUtils.borealisHandler = new BorealisHandler();
        _setAppScreenBrightness(_getSystemScreenBrightness());
        getContentResolver().registerContentObserver(
                Settings.System.getUriFor(Settings.System.SCREEN_BRIGHTNESS),
                true,
                brightnessObserver);
    }

    private void _setAppScreenBrightness(float value) {
        PlatformUtils.setAppScreenBrightness(this, value);
    }

    private float _getSystemScreenBrightness() {
        return PlatformUtils.getSystemScreenBrightness(this);
    }

    private final ContentObserver brightnessObserver = new ContentObserver(new Handler()) {
        @Override
        public void onChange(boolean selfChange) {
            _setAppScreenBrightness(_getSystemScreenBrightness());
        }
    };

    public static Surface getMpvSurface() {
        if (mpvSurface == null) {
            return null;
        }
        return mpvSurface.getHolder().getSurface();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        getContentResolver().unregisterContentObserver(brightnessObserver);

        // Android does not recommend using exit(0) directly,
        // but borealis heavily uses static variables,
        // which can cause some problems when reloading the program.

        // In SDL3, we can use SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY to control the behavior

        // In SDL2, Force exit of the app.
        System.exit(0);
    }

    @Override
    protected String[] getLibraries() {
        // Load SDL2 and borealis demo app
        return new String[] {
                "curl",
                "SDL2",
                "Switchfin"
        };
    }

}