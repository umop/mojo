// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.mojo.shell;

import android.content.Context;
import android.util.Log;

import dalvik.system.DexClassLoader;

import org.chromium.base.CalledByNative;
import org.chromium.base.JNINamespace;
import org.chromium.base.TraceEvent;

import java.io.File;
import java.lang.reflect.Constructor;

/**
 * Content handler for archives containing native libraries bundled with Java code.
 * <p>
 * TODO(ppi): create a seperate instance for each application being bootstrapped to keep track of
 * the temporary files and clean them up once the execution finishes.
 */
@JNINamespace("shell")
public class AndroidHandler {
    private static final String TAG = "AndroidHandler";

    // Bootstrap native and java libraries are packaged with the MojoShell APK as assets.
    private static final String BOOTSTRAP_JAVA_LIBRARY = "bootstrap_java.dex.jar";
    private static final String BOOTSTRAP_NATIVE_LIBRARY = "libbootstrap.so";
    // Name of the bootstrapping runnable shipped in the packaged Java library.
    private static final String BOOTSTRAP_CLASS = "org.chromium.mojo.shell.Bootstrap";

    // File extensions used to identify application libraries in the provided archive.
    private static final String JAVA_LIBRARY_SUFFIX = ".dex.jar";
    private static final String NATIVE_LIBRARY_SUFFIX = ".so";

    // Recursively finds the first file in |dir| whose name ends with |suffix|. Returns |null| if
    // none is found.
    static File find(File dir, String suffix) {
        for (File child : dir.listFiles()) {
            if (child.isDirectory()) {
                File result = find(child, suffix);
                if (result != null) return result;
            } else {
                if (child.getName().endsWith(suffix)) return child;
            }
        }
        return null;
    }

    /**
     * Extracts and runs the application libraries contained by the indicated archive.
     *
     * @param context the application context
     * @param tracingId opaque id, used for tracing.
     * @param extractedPath the path of the directory containing the application to be run
     * @param cachePath the path of a cache directory that can be used to extract assets
     * @param handle handle to the shell to be passed to the native application. On the Java side
     *            this is opaque payload.
     * @param runApplicationPtr pointer to the function that will set the native thunks and call
     *            into the application MojoMain. On the Java side this is opaque payload.
     */
    @CalledByNative
    private static boolean bootstrap(Context context, long tracingId, String extractedPath,
            String cachePath, int handle, long runApplicationPtr) {
        File extractedDir = new File(extractedPath);
        File cacheDir = new File(cachePath);
        File compiledDexDir = new File(cacheDir, "dex");
        File assetDir = new File(cacheDir, "asset");
        assetDir.mkdirs();
        File preparedSentinel = new File(cacheDir, "prepared");

        // If the sentinel doesn't exist, extract the assets from the apk.
        if (!preparedSentinel.exists()) {
            compiledDexDir.mkdirs();
            try {
                TraceEvent.begin("ExtractBootstrapJavaLibrary");
                FileHelper.extractFromAssets(context, BOOTSTRAP_JAVA_LIBRARY, assetDir, false);
                TraceEvent.end("ExtractBootstrapJavaLibrary");
                TraceEvent.begin("ExtractBootstrapNativeLibrary");
                FileHelper.extractFromAssets(context, BOOTSTRAP_NATIVE_LIBRARY, assetDir, false);
                TraceEvent.end("ExtractBootstrapNativeLibrary");
                TraceEvent.begin("MoveBootstrapNativeLibrary");
                // Rename the bootstrap library to prevent dlopen to think it is alread opened.
                new File(assetDir, BOOTSTRAP_NATIVE_LIBRARY)
                        .renameTo(File.createTempFile("bootstrap", ".so", assetDir));
                TraceEvent.end("MoveBootstrapNativeLibrary");
                new java.io.FileOutputStream(preparedSentinel).close();
            } catch (Exception e) {
                Log.e(TAG, "Extraction of bootstrap files from assets failed.", e);
                return false;
            }
        }
        // Find the 4 files needed to execute the android application.
        File bootstrap_java_library = new File(assetDir, BOOTSTRAP_JAVA_LIBRARY);
        File bootstrap_native_library = find(assetDir, NATIVE_LIBRARY_SUFFIX);
        File application_java_library = find(extractedDir, JAVA_LIBRARY_SUFFIX);
        File application_native_library = find(extractedDir, NATIVE_LIBRARY_SUFFIX);

        // Compile the java files.
        String dexPath = bootstrap_java_library.getAbsolutePath() + File.pathSeparator
                + application_java_library.getAbsolutePath();
        TraceEvent.begin("CreateDexClassLoader");
        DexClassLoader bootstrapLoader = new DexClassLoader(dexPath,
                compiledDexDir.getAbsolutePath(), null, ClassLoader.getSystemClassLoader());
        TraceEvent.end("CreateDexClassLoader");

        // Create the instance of the Bootstrap class from the compiled java file and run it.
        try {
            Class<?> loadedClass = bootstrapLoader.loadClass(BOOTSTRAP_CLASS);
            Class<? extends Runnable> bootstrapClass = loadedClass.asSubclass(Runnable.class);
            Constructor<? extends Runnable> constructor = bootstrapClass.getConstructor(
                    Context.class, long.class, File.class, File.class, int.class, long.class);
            Runnable bootstrapRunnable = constructor.newInstance(context, tracingId,
                    bootstrap_native_library, application_native_library, Integer.valueOf(handle),
                    Long.valueOf(runApplicationPtr));
            bootstrapRunnable.run();
        } catch (Throwable t) {
            Log.e(TAG, "Running Bootstrap failed.", t);
            return false;
        }
        return true;
    }

}
