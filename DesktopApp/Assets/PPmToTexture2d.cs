using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System.IO;
using System;
using MLAPI;
public class PPmToTexture2d : NetworkedBehaviour
{
    [HideInInspector] public Texture2D m_textureToSend = null;
    //public Texture2D m_receivedTexture;
    public int count;
    [HideInInspector] public string ImagePath;
    [HideInInspector] public Renderer m_myRenderer;
    // Start is called before the first frame update
    private void Start()
    {
        m_myRenderer = gameObject.GetComponent<Renderer>();
        ImagePath = "C:/Users/sharm/Desktop/TusharImages/quarter_" + count + ".ppm";
        // gameObject.GetComponent<Renderer>().material.mainTexture = ReadBitmapFromPPM("C:/Users/tpura/Desktop/CIS 565 GPU/remote-rendering/output/quarter_" + count + ".ppm");
        //ReadBitmapFromPPM("C:/Users/tpura/Desktop/CIS 565 GPU/remote-rendering/output/quarter_" + count + ".ppm");
        if (NetworkingManager.Singleton.IsServer&& transform.root.GetComponent<NetworkedObject>().IsLocalPlayer)
        {
            StartCoroutine(LoadFile(ImagePath));
        }
    }

    //void loadImage()
    //{
    //    // count = (count + 1) % 3;
    //    //byte[] file = File.ReadAllBytes("C:/Users/tpura/Desktop/CIS 565 GPU/remote-rendering/optix_sdk_7_2_0/SDK/build/optixPathTracer/oImage" + count + ".png");
    //    byte[] file = File.ReadAllBytes("C:/Users/tpura/Desktop/CIS 565 GPU/remote-rendering/optix_sdk_7_2_0/SDK/build/optixPathTracer/quarter_1.ppm");

    //    m_receivedTexture = new Texture2D(1, 1);
    //    m_receivedTexture.LoadImage(file);
    //    gameObject.GetComponent<Renderer>().material.mainTexture = m_receivedTexture;
    //}

    // Update is called once per frame
    void Update()
    {
        //ReadBitmapFromPPM("C:/Users/tpura/Desktop/CIS 565 GPU/remote-rendering/output/quarter_" + count + ".ppm");

    }

    IEnumerator LoadFile(String file)
    {
        while (true)
        {
            yield return new WaitForSeconds(0.1f);

            FileStream f = new FileStream(file, FileMode.Open);
            var reader = new BinaryReader(f);
            if (reader.ReadChar() != 'P' || reader.ReadChar() != '6')
                continue;
            reader.ReadChar(); //Eat newline
            string widths = "", heights = "";
            char temp;
            while ((temp = reader.ReadChar()) != ' ')
                widths += temp;
            while ((temp = reader.ReadChar()) >= '0' && temp <= '9')
                heights += temp;
            if (reader.ReadChar() != '2' || reader.ReadChar() != '5' || reader.ReadChar() != '5')
                continue;
            reader.ReadChar(); //Eat the last newline
            int width = int.Parse(widths),
                height = int.Parse(heights);

            //Debug.Log(width + " " + height);


            //Debug.Log(reader.ReadByte() + " " + reader.ReadByte() + " " + reader.ReadByte());
            Texture2D tex = new Texture2D(width, height);
            //Read in the pixels
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    //Debug.Log(reader.ReadByte() + " " + reader.ReadByte() + " " + reader.ReadByte());
                    int r = reader.ReadByte();
                    int g = reader.ReadByte();
                    int b = reader.ReadByte();
                    tex.SetPixel(x, y, new Color(r / 255.0f, g / 255.0f, b / 255.0f));
                    /*tex.SetPixel(x, y, new Color()
                    {
                        r = reader.ReadByte() ,
                        g = reader.ReadByte() ,
                        b = reader.ReadByte()
                    }); ;*/
                }
            tex.Apply();
            f.Close();
            m_textureToSend = tex;
            m_myRenderer.material.mainTexture = tex;
        }
    }

    public void ReadBitmapFromPPM(string file)
    {
        FileStream f = new FileStream(file, FileMode.Open);
        var reader = new BinaryReader(f);
        if (reader.ReadChar() != 'P' || reader.ReadChar() != '6')
            return;
        reader.ReadChar(); //Eat newline
        string widths = "", heights = "";
        char temp;
        while ((temp = reader.ReadChar()) != ' ')
            widths += temp;
        while ((temp = reader.ReadChar()) >= '0' && temp <= '9')
            heights += temp;
        if (reader.ReadChar() != '2' || reader.ReadChar() != '5' || reader.ReadChar() != '5')
            return;
        reader.ReadChar(); //Eat the last newline
        int width = int.Parse(widths),
            height = int.Parse(heights);

        Debug.Log(width + " " + height);


        //Debug.Log(reader.ReadByte() + " " + reader.ReadByte() + " " + reader.ReadByte());
        Texture2D tex = new Texture2D(width, height);
        //Read in the pixels
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                //Debug.Log(reader.ReadByte() + " " + reader.ReadByte() + " " + reader.ReadByte());
                int r = reader.ReadByte();
                int g = reader.ReadByte();
                int b = reader.ReadByte();
                tex.SetPixel(x, y, new Color(r / 255.0f, g / 255.0f, b / 255.0f));
                /*tex.SetPixel(x, y, new Color()
                {
                    r = reader.ReadByte() ,
                    g = reader.ReadByte() ,
                    b = reader.ReadByte()
                }); ;*/
            }
        tex.Apply();
        f.Close();
        m_myRenderer.material.mainTexture = tex;
    }
}
